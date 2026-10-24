# The programming port for KST and Chaservo servos

<sub>**English** · [Deutsch](LinkKST-de.md)</sub>

This page is a section of [The link](Link.md), which holds the wiring, the frame
format, the page map and the other pages. Pins are named by GPIO (general-purpose
input/output) number. PWM is pulse-width modulation.

The servo programming page (`KST`, 0x32) is new in 4.12 and names a
programming port since 4.13. The page
runs the programming protocol of
[`protocols/kst`](../protocols/kst/README.md) for KST and Chaservo servos,
which share one protocol and one register set, on one port of
[PROG](Link.md#the-programming-ports) at a time. A port renders PWM pulses or runs
the programming protocol, never both. Whether a KST or Chaservo servo in
programming mode follows PWM pulses is not measured. A standard servo port
is no programming port and a command that names one is refused.

The session runs on the IOmcu with the module's planner and limits.
The panel writes an operation and the images it needs. It reads the state,
the result and the servo's 32 registers. It sends no register write.
`shared/outputs/kst_port.c` holds the rules. Nothing is kept in flash: after
a restart of the IOmcu the port is in PWM with no session and bit 4 of
register 0 clear, whatever mode the servo is in, and the supply is asked
off.

The session plans a WRITE and a RESTORE itself (since 4.14). The command's
frame checks the request and prepares the planner's job, 64us of planner
work. A target the planner refuses without a search is refused there
(PLAN_RULES, PLAN_R00, PLAN_PAIRING) and no operation starts. Every other
request starts the operation, and each pass of core 0's main loop then does
one slice of the planner: 100 to 109 units of work, 892us at most. While
the port plans, bit 3 of register 0 is set, the low byte of `RESULT` reads
35, the frame count reads 0 and no frame goes to the servo. The planning
ends after 216,000 units at most, 2,161 passes:

- with a plan: `RESULT` reads 1 and the session reads the servo and writes;
- with no order of writes that keeps every image in between valid: result
  33;
- with the 216,000 units spent before either is known: result 34. The
  request is not known to have no plan;
- a RESTORE whose result cannot be checked, without the confirmation:
  result 11.

Nothing is sent to the servo for results 33, 34 and 11, the session is not
locked, and the next WRITE is taken without a READ_ALL. The costliest
request found takes 120,136 units, 1,202 passes at most and 397 to 647ms
of planner time. The times are computed in an emulator from the
instructions executed at 150MHz
([Performance](Performance.md#the-iomcu)) and not measured on
hardware. The time of a pass with the rest of the main loop in it is not
measured, so the planning time on a bench is not known. A 4.13 IOmcu
runs the whole search inside the command's frame and answers a request
without an order of writes with the refusal PLAN_NO_PATH.

The port, the page and the panel's module run in the host suite against a
servo, line and supply model.

| State | Value | The port's pin | The supply | Ends |
| --- | ---: | --- | --- | --- |
| PWM | 0 | the PWM generator | the panel's, on SUPPLY | when ENTER or REBOOT is taken |
| STOPPING | 1 | the PWM generator, rendering no pulse: a pulse in progress completes | unchanged | after 50ms |
| LOW | 2 | the wire driver, driven low | set for programming, see below | after 100ms and when the supply reads as asked |
| PROGRAMMING | 3 | the wire driver: low, a frame, or an input for a reply window | on, held by the port | on POWER_CYCLED, REBOOT or LEAVE, and when the supply goes off; before the first frame also on ABORT, a stop or a failed entry |
| RAIL_OFF | 4 | the wire driver, or the PWM generator rendering no pulse after a REBOOT in PWM | off | 2000ms after the supply reads off |
| RAIL_WAIT | 5 | as RAIL_OFF | on, at the set points it had | 1000ms after the supply reads on, to PWM |
| LEAVING | 6 | the wire driver, driven low | as it is: not switched, no set point written | after the wait of write register 28, to PWM (since 4.15) |

The entry sequence of the session holds the line low for another 100ms
after LOW. From LOW on the generator does not have the pin. Every source of
a PWM command (the panel, the sweep, the automatic test) ends at the port's
bank channel, so none reaches the pin; PROG and SERVO still take such a
command. The port's `PWM` bit and `TARGET` stay as written through a
session, and the pulse is back when the port is in PWM again. The
IOmcu writes no flash while the port is out of PWM.

## The supply

The port's servo is supplied by the PD mini of SUPPLY (0x2A). Bit 10 of
register 0 reads 1 while a PD mini answers; the port then switches it.

ENTER takes the programming voltage and current limit from write registers
26 and 27. With a PD mini answering, ENTER is refused with SUPPLY_RANGE
unless the voltage is at most 8400mV, the current 50 to 3000mA and the
session's voltage at least 700mV under the input voltage of the PD mini. 8400mV is the highest
servo rail of the IO board. The session's voltage is the staged one, or
the lower of it and the voltage of an output that is on; under 4800mV
ENTER is refused with SUPPLY_LOW (since 4.15) and the supply is not
touched. The panel holds the same floor on its side: it sends no ENTER
while it reads a PD mini output that is on and set under 4800mV, whatever
the IOmcu does with that ENTER. 4800mV is `KST_PORT_SUPPLY_MIN_MV`; the supply
voltage a servo needs to store a register is not measured. In LOW the port
then does one of two things:

- the output is asked off: the set points are written and the output is
  switched on. The session starts 1000ms after the PD mini reads on;
- the output is on: the voltage is set to the lower of the two voltages and
  the current limit to the lower of the two limits (the limit since 4.15).
  Nothing is written where both are the output's already.

No step of the port raises the voltage or the current limit of an output
that is on, and leaving programming does not restore a higher value that
ENTER lowered. A PD mini
that does not read as asked within 3000ms ends ENTER with result 32: the
port is in PWM, and an output switched on by this ENTER is off again.

POWER_CYCLED and REBOOT in PROGRAMMING, with the PD mini asked on, cycle
the supply: off, RAIL_OFF for 2000ms from the read that shows it off, on at
the set points it had, RAIL_WAIT for 1000ms from the read that shows it on,
then PWM with bit 4 clear. REBOOT is also taken in PWM, for a servo that is
to restart: the same cycle, with no pulse on the port during it. 2000ms and
1000ms are the owner's values. The time a KST servo needs without power to
leave programming mode, the time it needs to start, and the supply voltage
it accepts in programming mode are not measured. Neither is the time the PD
mini takes to switch.

With no PD mini answering the port touches no supply. The session runs, bit
10 reads 0, POWER_CYCLED is the operator's word that the servo was without
power and returns the port to PWM at once, and REBOOT is refused with
NO_SUPPLY. REBOOT is refused the same way while the PD mini is asked off.

While the port is out of PWM, bit 11 of SUPPLY's flags is set (since 4.13)
and SUPPLY refuses with BAD_VALUE every write of its registers 4 to 6 that
is not an OFF: an ON of an output that is off, and any change of the
voltage or of the current limit, up or down (since 4.15; 4.13 and 4.14
refuse an ON and a raise of the voltage only). A write with OUTPUT 0 is
taken as an OFF whatever set points come with it, and the page keeps the
set points it had. A write that repeats an output that is on with the set
points in force is taken. The port does not switch the output on while
supply wiring just written is not yet in flash.

An OFF written to SUPPLY, or the heartbeat lost, while an operation of the
port runs reads 0 in OUTPUT at once and reaches the PD mini when the port
has cut its frame and no operation runs, 12ms after it at the latest
(`SUPPLY_PAGE_OFF_WAIT_MS`, since 4.15). The port sees OUTPUT 0 in its next
pass and ends the operation. An output the PD mini switches off by itself
is not delayed. The time the PD mini's output takes to fall after the
command is not measured.

The IOmcu does not switch the bank to ARMED while a programming port is out of
PWM: the ARM register is taken and the bank stays SAFE until the port
is in PWM again. ENTER is refused with BUSY while the bank is ARMED (both
since 4.15).

Where the supply ends when something fails:

| Event | The supply | The port |
| --- | --- | --- |
| The PD mini does not answer at ENTER | untouched | the session runs as on a bench without one |
| The PD mini stops answering or does not read as asked within 3000ms in LOW | asked off if this ENTER switched it on, else left on, at the lower voltage if this ENTER lowered it | PWM, result 32 |
| The staged voltage is out of range | untouched | ENTER refused with SUPPLY_RANGE |
| The session's voltage is under 4800mV | untouched | ENTER refused with SUPPLY_LOW |
| The supply reads off or is asked off in PROGRAMMING (the panel, a lost heartbeat, the PD mini's own protection) | kept off for 2000ms, not switched on again | the operation ends, a frame on the wire is finished; PWM with bit 4 clear, result 19 |
| The supply does not read off within 3000ms of a cycle's start | asked off | from PROGRAMMING: PROGRAMMING again with bits 4 and 5 kept, result 32. From PWM: PWM, result 32 |
| The heartbeat is lost or the link is silent in RAIL_OFF | stays off | PWM with bit 4 clear, result 19 |
| The supply does not read on within 3000ms in RAIL_WAIT | asked off | PWM, result 32 |
| The supply is asked off in RAIL_WAIT (the panel, a lost heartbeat) | off | PWM, result 19 |

The supply ends off whenever no panel is there to switch it off: SUPPLY
switches the PD mini off when the heartbeat stops, and the port switches
nothing on while the heartbeat is not trusted. A servo without power is in
no mode, so a port that returns to PWM this way has bit 4 clear.

## Leaving without a supply cycle

LEAVE (operation 10, since 4.15) returns a port from PROGRAMMING to PWM with
the supply as it is. The port sends no frame, switches nothing and writes no
set point. An output that is on stays on at its voltage and current limit,
also one that ENTER switched on or lowered. With no PD mini answering the
port does the same. POWER_CYCLED and REBOOT stay as they are.

A LEAVE that is taken puts the port in LEAVING. The wire driver holds the
pin low for the wait in write register 28 (`LEAVE_MS`), 0 to 10,000ms,
counted from the command. The wait is 100ms until a panel writes the
register, and the register keeps what was written until the IOmcu
restarts. The register takes every value: a LEAVE that finds more than 10,000 in it is refused with LEAVE_MS,
the port stays in PROGRAMMING, and the value is not clamped. A write to the
register while the port waits is for the next LEAVE. After the wait the
generator has the pin, the port is in PWM with bits 4 and 5 clear and result
0, and the port's pulse is back as its `PWM` bit and `TARGET` say. A wait of
0ms ends at the next pass of the main loop. The time left is not on the
page: all 32 read registers are in use. A panel counts it from its own
clock.

Not measured on a KST or Chaservo servo: the time the line has to be low
before the first PWM pulse, whether the servo follows PWM while it is in
programming mode, and whether a written value acts before the servo's next
power-up. Bit 4 clear after a LEAVE says that the port holds no session. It
does not say that the servo has left programming mode.

| The port at the command | LEAVE |
| --- | --- |
| the heartbeat not trusted or the link silent, in any state | refused with UNSAFE |
| PWM | refused with NOT_PROGRAMMING |
| STOPPING, LOW, RAIL_OFF, RAIL_WAIT, LEAVING | refused with BUSY; a wait that runs is not started again |
| PROGRAMMING, an operation runs or plans | refused with BUSY. The operation goes on; ABORT ends it |
| PROGRAMMING, another port named | refused with PORT |
| PROGRAMMING, bit 5 set | refused with MUST_READ: a session that was cut is not left before a READ_ALL has succeeded |
| PROGRAMMING, bit 9 set | refused with LOCKED: a servo that a write left in doubt is left by RESTORE and then LEAVE, or by a supply cycle |
| PROGRAMMING, register 28 above 10,000 | refused with LEAVE_MS |
| PROGRAMMING, none of these | taken: LEAVING, bit 3 set, result 1 |

A refused LEAVE changes nothing: no frame, no supply command, the pin and
the state as they were.

| In LEAVING | The supply | The port |
| --- | --- | --- |
| The wait ends | as it is | PWM with bits 4 and 5 clear, result 0 |
| ABORT | as it is | PROGRAMMING with result 19, the session as LEAVE found it, bit 5 not set |
| A switch to SAFE | as it is | PROGRAMMING with result 19 and bit 5 set |
| The heartbeat lost or the link silent, no PD mini answering | untouched | PROGRAMMING with result 19 and bit 5 set |
| The supply reads off or is asked off (the panel's STOP, a lost heartbeat, the PD mini's own protection) | kept off for 2000ms, not switched on again | RAIL_OFF, then PWM with bits 4 and 5 clear and result 19 |
| The PD mini stops answering | untouched | the wait goes on |
| Any other command | as it is | refused with BUSY |

A stop in the pass in which the wait ends comes first: the port is in
PROGRAMMING.

## Registers

A read gives these registers:

| Register | Name | Reads |
| ---: | --- | --- |
| 0 | `STATE` | bits 0 to 2 the state. Bit 3 an operation runs. Bit 4 a frame was sent since the servo was last without power or the port was last left with LEAVE. Bit 5 a stop or an abort cut the session: READ_ALL comes before anything else. Bit 6 an image was read. Bit 7 the session holds a backup. Bit 8 the image has the known layout. Bit 9 only RESTORE is accepted. Bit 10 a PD mini answers and the port switches the servo's supply. Bit 11 registers 16 to 31 show the backup. Bits 12 to 15 the programming port |
| 1 | `SEQ` | the `SEQ` of the last command taken; 0 at start |
| 2 | `OP` | bits 12 to 15 the last operation started, 0 at start and until an operation starts; a refused command leaves them. Bits 0 to 11 the frames it has sent, held at 4095 |
| 3 | `RESULT` | low byte the result of that operation, 1 while it runs, 35 while a WRITE or a RESTORE plans (since 4.14). High byte why the last command taken started nothing, 0 when it started |
| 4 | `FAIL` | low byte the servo register the result names, 0xFF for none. High byte the writes of the plan that are done |
| 5, 6 | `DIFF` | bit n: servo register n differs from what the operation expected. Register 5 holds servo registers 0 to 15 |
| 7, 8 | `BAD` | bit n: servo register n has no 3 equal reads in 5 |
| 9 | `FP_RULES` | bit n: rule n of the layout fingerprint is broken |
| 10, 11 | `FP_REGS` | bit n: servo register n deviates from the fingerprint |
| 12, 13 | `HALF_MIN_NS`, `HALF_MAX_NS` | the lowest and highest half-cell measured on the servo's replies since the port took the pin, ns; 0 before the first reply |
| 14, 15 | `DELAY_MIN_US`, `DELAY_MAX_US` | the last edge of a read frame to the first edge of its reply, µs; 0 before the first |
| 16 to 31 | `IMAGE` | servo register 2 k in the low byte of register 16 + k, 2 k + 1 in the high byte. The image last read, or the backup while `VIEW` is 1; 0 while there is none |

A write sees other registers:

| Register | Name | Takes |
| ---: | --- | --- |
| 0 | `CMD` | bits 0 to 3 the operation, bit 6 set: the command names a programming port (since 4.13), bit 7 the confirmation of an unchecked restore (RESTORE only), bits 8 to 11 the port; every other bit 0 |
| 1 | `SEQ` | the page's `SEQ` plus 1, modulo 65536 |
| 2 | `KEY` | 0x57A5 for WRITE, RESTORE and RELEASE, else 0 |
| 3 | `CRC` | the check over registers 4 to 24, for WRITE, RESTORE, RELEASE and VERIFY; not looked at for the others |
| 4 to 19 | `IMAGE` | packed as the read's registers 16 to 31: the target of a WRITE, the backup of a RESTORE, the image a VERIFY expects |
| 20 to 23 | `UNLOCK` | low word first: the fields a WRITE may change beyond the editable class, bit n for field n of `kst_field_id_t` |
| 24 | `START_CRC` | the check over the 16 registers of the image the panel planned from, as the read gave them |
| 25 | `VIEW` | 0: the read's registers 16 to 31 show the image last read. 1: the backup. In force when written |
| 26 | `SUPPLY_MV` | the supply voltage of an ENTER, mV (since 4.13). The panel's default is 5000 |
| 27 | `SUPPLY_MA` | the current limit of an ENTER that switches the supply on, mA (since 4.13) |
| 28 | `LEAVE_MS` | the wait of a LEAVE with the line low, 0 to 10,000ms (since 4.15). 100 until it is written |

The check is the link's CRC-16 (cyclic redundancy check), seeded 0xFFFF,
over each register's low byte and then its high byte (`link_kst_crc()`).

Registers 0 to 3 are the command: one frame, written as exactly those 4
registers and taken whole or not at all. Registers 4 to 28 are written in
any windows before the command. A write that reaches register 29 or higher
is refused with READ_ONLY, a window that touches registers 0 to 3 and is
not exactly them with BAD_RANGE. A command with a reserved bit, an operation
outside 1 to 10, the confirmation on another operation than RESTORE, a `KEY`
that is neither 0 nor 0x57A5 or a `SEQ` other than the page's plus 1 is
refused with BAD_VALUE and changes nothing. The frame last taken, sent
again, is acknowledged and does nothing, so a command whose answer was lost
is sent again as it was. After an abort at the panel it is not sent
again: the panel reads registers 0 to 3. The `SEQ` before the command
means it was not taken, and the request counts as dropped. The command's
`SEQ` means it was taken only together with its operation in `OP` or a
refusal in `RESULT`'s high byte. Any other `SEQ` is an IOmcu that
started again. An IOmcu that starts again reads `SEQ` 0, operation 0
and no refusal. Operation 0 after a read that named an operation is a
restart at any `SEQ`, the command's and the one before it included: a
fresh page's `SEQ` 0 is the command's after `SEQ` 0xFFFF and the one
before it after `SEQ` 0, and a fresh page takes a command with `SEQ` 1
sent again as its own next command. The panel then reports a restart and
no result. An IOmcu that has taken no command since it started reads
`SEQ` 0 and operation 0 before and after a start: when it starts again
between its first command and that read, the request counts as dropped.
Of such first commands only ENTER and REBOOT start an operation; every
write needs PROGRAMMING, which an ENTER starts. A command that is taken is
acknowledged; what became of it is in `RESULT`.

| Operation | Value | Does |
| --- | ---: | --- |
| ENTER | 1 | in PWM: takes the port's pin, sets the supply and runs the entry sequence. In PROGRAMMING: runs the entry sequence again |
| READ_ALL | 2 | reads the 32 registers, each until 3 of at most 5 reads agree. The first image of a session is its backup |
| WRITE | 3 | plans from the image read to the staged image, one slice a pass (since 4.14), and runs the plan: every write is read back, and a write that does not take is undone |
| RESTORE | 4 | plans back to the backup the session holds and runs the plan. A staged image that is not that backup ends it with result 10 |
| RELEASE | 5 | releases the pairing |
| VERIFY | 6 | reads all registers and compares them with the staged image |
| ABORT | 7 | ends the running operation. In STOPPING and LOW the port returns to PWM. In RAIL_OFF the supply is not switched on again, in RAIL_WAIT it is switched off, and the cycle ends with result 19. Taken in every state; register 2 does not name it |
| POWER_CYCLED | 8 | in PROGRAMMING: cycles the supply and returns the port to PWM. With no PD mini answering or the PD mini asked off: the operator's word that the servo was without power, to PWM at once |
| REBOOT | 9 | cycles the supply, in PROGRAMMING and in PWM (since 4.13) |
| LEAVE | 10 | in PROGRAMMING with no operation running: holds the line low for the wait of register 28 and returns the port to PWM. The supply is not switched (since 4.15) |

The result in the low byte of `RESULT` is the session's `kst_ses_t`: 0 done,
1 running, 4 no servo answered, 5 not in programming mode, 6 a register
without 3 equal reads, 7 no backup, 8 the layout is not the known one, 9
locked to a restore, 10 a plan the session does not accept, 11 a restore
that needs the confirmation, 12 the servo's image changed since it was
read, 13 a write did not take and nothing changed, 14 a plan undone, 15 a
plan undone in part, 16 a register changed that the plan did not write, 17
the image differs after the plan, 18 the wire driver failed or could not be
opened, 19 aborted. 32 to 35 are the port's own. 32 (since 4.13): the
supply did not switch in time. 33 (since 4.14): no order of writes keeps
every image in between valid. 34 (since 4.14): the planner spent 216,000
units of work before it had a plan or knew there is none. 35 (since 4.14):
the operation plans; it is never the result of an operation that has
ended.

| Refusal | Value | The command started nothing because |
| --- | ---: | --- |
| BUSY | 1 | an operation runs or the port is between two states |
| PORT | 2 | the build has no such programming port, the command lacks bit 6, or the port is not the one in PROGRAMMING |
| UNSAFE | 5 | the heartbeat is not trusted or the link is silent |
| NOT_PROGRAMMING | 6 | the port is in PWM |
| NO_ENABLE | 7 | WRITE, RESTORE or RELEASE without the key |
| STAGED | 8 | registers 4 to 24 do not have the check the command names |
| START | 9 | the image read is not the one the panel planned from |
| MUST_READ | 10 | a stop or an abort cut the session and no READ_ALL has succeeded since |
| NO_IMAGE | 11 | no image was read to plan from |
| PLAN_RULES | 12 | the target breaks a hard rule |
| PLAN_R00 | 13 | the target differs in servo register 0x00 |
| PLAN_PAIRING | 14 | a WRITE differs in servo register 0x1D |
| PLAN_NO_PATH | 15 | no order of writes keeps every image in between valid (4.12 and 4.13 only; result 33 since 4.14) |
| NO_PIN | 16 | the port holds no pin, or its FUNC is DShot (since 4.28), for an ENTER and for a REBOOT in PWM (since 4.13) |
| SUPPLY_RANGE | 17 | the staged supply voltage or current is outside what ENTER takes (since 4.13) |
| NO_SUPPLY | 18 | a REBOOT with no PD mini answering or the PD mini asked off (since 4.13) |
| LEAVE_MS | 19 | a LEAVE with more than 10,000ms in register 28 (since 4.15) |
| LOCKED | 20 | a LEAVE while only RESTORE is accepted (since 4.15) |
| SUPPLY_LOW | 21 | an ENTER whose supply voltage, the staged one or the lower one of an output that is on, is under 4800mV (since 4.15) |

Values 3 and 4 are not given. A result the session knows without a plan
(5, 7, 8, 9, and 10 for a RESTORE whose staged image is not the backup) is
given before PLAN_RULES, PLAN_R00 and PLAN_PAIRING (since 4.14): a locked
session answers a WRITE with result 9 whatever the target is. NO_ENABLE is
checked before every other
reason. The panel sends the key only while its switch ENABLE WRITE TO SERVO
is on, so a command that reaches the IOmcu without it writes nothing
in any state of the port. No screen holds that switch in this build.

A programming port is bidirectional by definition: its pin is a bare GPIO
that the wire driver turns between output and input. A servo behind an
output-only buffer ends ENTER with result 4.

## STOP, switch to SAFE, heartbeat loss and link loss

The port sees three things. A stop is the failsafe's edge (the heartbeat
stopped or the link went silent) or the edge of the bank's switch to SAFE. Unsafe is
the time the heartbeat is not trusted or the link is silent. The third is
the supply: SUPPLY switches the PD mini off when the heartbeat stops, and
the panel writes SUPPLY's output off at every STOP.

The panel's STOP is therefore three things at the IOmcu: a switch to SAFE of
a bank in ARMED, SUPPLY's output written 0, and the command ABORT from
`kst_link_abort()`.

| State | A switch to SAFE alone | The panel's STOP | Heartbeat lost, link silent |
| --- | --- | --- | --- |
| PWM | the pulse stops, as on every output. The supply stays as it is | the pulse stops and the supply is off | the pulse stops. SUPPLY switches the PD mini off |
| STOPPING, LOW | the port returns to PWM with result 19. A supply this ENTER switched on is switched off; one found on stays on | the same, and the supply is off | the same, and SUPPLY switches the PD mini off |
| PROGRAMMING | the running operation ends with result 19: a frame on the wire is finished, which takes at most 12ms, and no other starts. The pin is driven low or an input afterwards. After a first frame bit 5 is set and the port stays PROGRAMMING; before it the port returns to PWM. The supply stays on | the operation ends the same way. The supply is off: the port keeps it off for 2000ms and returns to PWM with bits 4 and 5 clear and result 19. With no PD mini answering the port stays PROGRAMMING with bit 5 set | as the panel's STOP |
| RAIL_OFF | nothing: the cycle goes on | the supply stays off; after the 2000ms the port is in PWM with result 19 | as the panel's STOP |
| RAIL_WAIT | nothing: the cycle goes on | the supply is switched off; the port is in PWM with result 19 | as the panel's STOP |
| LEAVING | the wait ends with result 19 and the port is in PROGRAMMING with bit 5 set. The supply stays on | the wait ends the same way. The supply is off: the port keeps it off for 2000ms and returns to PWM with bits 4 and 5 clear and result 19. With no PD mini answering the port stays PROGRAMMING | as the panel's STOP |

While bit 5 is set the port takes ENTER, READ_ALL, ABORT, POWER_CYCLED and
REBOOT only, until a READ_ALL has succeeded. ABORT alone has the effect of
a switch to SAFE in STOPPING, LOW and PROGRAMMING. In LEAVING it returns the port to
PROGRAMMING without bit 5.

An operation that plans is an operation in PROGRAMMING, and the row above
holds for it: a switch to SAFE, the panel's STOP, a lost heartbeat, a silent link,
an ABORT and a supply found off end the planning at the next pass with
result 19. No frame was sent and no register written, and the session is
not locked. Bit 5 is set as after a cut frame, because the session sent
frames before it planned. The supply stays on after a switch to SAFE or an ABORT
alone. After the panel's STOP, a lost heartbeat or a silent link it is off,
and with a PD mini answering the port keeps it off for 2000ms and returns
to PWM with bits 4 and 5 clear. POWER_CYCLED and REBOOT end the planning
and then run. Every other command is refused with BUSY and the planning
goes on. A PD mini that stops answering does not end it. A switch to SAFE is seen as an edge: the port stops once and takes commands
again while the bank stays SAFE, so a servo is read and written on a
bench that is SAFE. While the heartbeat is not trusted or the link is silent no
frame starts and every operation but ABORT and POWER_CYCLED is refused with
UNSAFE.

A link lost during RAIL_OFF leaves the supply off and the port in PWM: with
no panel on the link nobody could switch the supply off again, and a servo
that had no power for 2000ms has left programming mode if 2000ms is
enough, which is not measured. The panel switches the supply on again on
SUPPLY.

| Panel | IOmcu | Servo programming page (`KST`, 0x32) |
| --- | --- | --- |
| 4.15 or newer | 4.15 or newer | served |
| 4.15 or newer | 4.14 | served without LEAVE. A 4.14 IOmcu refuses operation 10 with BAD_VALUE and a write to register 28 with READ_ONLY: the panel's module sends neither and answers the request with `KST_LINK_START_NO_OP`. POWER_CYCLED and REBOOT are the ways out |
| 4.14 | 4.15 or newer | served. The panel sends no operation 10 and no register 28, and the port is never in LEAVING |
| 4.14 or newer | 4.13 | served, from 4.15 without LEAVE as above. The IOmcu plans inside the command's frame: the panel's module never reports PLANNING, and a request without an order of writes is the refusal PLAN_NO_PATH |
| 4.13 | 4.14 or newer | served. The panel's module reads an operation that plans as one that runs, and results 33 and 34 as results it has no name for |
| 4.13 or newer | 4.12 | the page names an output channel there; the panel's module reads the minor at link-up and sends nothing below 13, not a write and not a read |
| 4.13 or newer | 4.11 or older | no page: the IOmcu answers a request to 0x32 with BAD_PAGE, and the panel sends none |
| 4.12 | 4.13 or newer | a 4.12 command has bit 6 clear and is refused with PORT; a write to register 26 or 27 never comes. The port stays in PWM |
| 4.11 or older | 4.13 or newer | the panel never reads or writes 0x32; the port stays in PWM |

`shared/bench/kst_link.c` is the panel's half. A request names the port,
for ENTER the supply voltage and current, and for LEAVE the wait, which the
module refuses above 10,000ms before anything is sent. It stages the registers,
sends the command with the next `SEQ`, reads registers 0 to 3 every 50ms
while an operation runs or the port is out of PWM and every 500ms otherwise
(one request and one data frame, 0.31ms of bus time), and after an
operation reads registers 4 to 15, the backup and the image. Its phase is
PLANNING while the page last read has bit 3 set and result 35, and RUNNING
before the first read and from the plan on. It sends a
command again up to 3 times when no answer comes, and not once after
`kst_link_abort()`, and reads `SEQ` again after
a refusal with BAD_VALUE. After a refused ABORT that read keeps the checks
above: a page off the panel's `SEQ`, or with operation 0 after a read that
named one, ends the operation that runs as restarted, never as done. An
ABORT refused with BAD_VALUE goes again after the read, with the page's
`SEQ`. A read of registers 0 to 3 refused with BAD_PAGE
means an IOmcu without the page; refused with any other code it counts
as unanswered and is sent again. An IOmcu without the page keeps, for
the caller, whether the port was out of PWM at the last read that answered.
Each request has a number, and its outcome carries that number. It refuses WRITE, RESTORE and RELEASE without the
caller's switch before anything is sent. `shared/bench/supply_link.c` takes
a command the port left on SUPPLY as the one asked and does not write its
own over it (`supply_link_adopted()`). The SERVO screen's programmer is
the caller ([Servo programmer](ServoProgrammer.md)).

`test_kst_port` runs the port and the page against the servo and supply
models: every transition and every refusal, a stop in each state and in the
middle of a frame, a write that plans for 864 passes with one slice in
each, an ABORT, a switch to SAFE, a lost heartbeat, a supply switched off, a
POWER_CYCLED, a REBOOT and a second command while it plans, a request with
no order of writes, one above the work cap, a write without the key, a PD
mini absent, slow, silent
and refusing, an ABORT in RAIL_OFF and in RAIL_WAIT, the supply found on above, at and under the programming
voltage, a link lost in RAIL_OFF and in RAIL_WAIT, the counters at their
ends and every timer across the 2^32ms wrap. For LEAVE they run the
command in every state, with a PD mini on, switched on by the entry, silent
and absent, during a frame and while the port plans, with bit 5 and with bit
9 set, waits of 0, 1, 100 and 10,000ms, the refusal at 10,001ms, and an
ABORT, a switch to SAFE, a lost heartbeat and a supply switched off in the wait.
`test_kst_link` runs the panel's module through the dispatcher
against the port: whole sessions, a write from PLANNING to its end, results
33 and 34, an abort and a stop while it plans, a LEAVE with its wait and
one aborted in the wait, lost answers, a stale `SEQ`, a restart of
the IOmcu and a 4.14, a 4.13, a 4.12 and a 4.11 IOmcu.
