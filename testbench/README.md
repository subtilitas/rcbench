# The measurement bench

A Raspberry Pi 5, a Kingst LA2016 logic analyser and an RP2350 board, wired so
that this project's protocol claims can be measured instead of asserted, and
driven over SSH (secure shell) so a measurement can be repeated without
anybody holding a probe.

Everything in `firmware/` is outside the host test suite, and every bit timing
in the tree was written from a specification and exercised only against frames
the same code builds. `docs/FirstRun.md` section 7 is the list of numbers
nobody has measured. This bench is what answers it.

**Nothing here has been run.** The hardware is being assembled, the channel
map is a proposal to be checked against the wiring as built, and three of the
pieces are described rather than written -- the panel's debug touch address,
the decoders and the recipes. Each says so where it is described, and they are
listed together under *What is not built yet*.

**A reading taken from a host is a reading at a moment.** Several of the
numbers recorded here are power-on values that the act of using the bench
changes: a GPIO's pull is what the pad comes up with and reads as `pn` once
OpenOCD has driven the line, and the PCIe power policy is what boot left
unless somebody has set it. Any such number is written with the condition it
holds under, at the number rather than in a footnote, and `host/selftest.sh`
prints what it finds beside the power-on value rather than asserting the
documented one. A reading that disagrees is shown as a disagreement; it is not
a failure, because after a check has run the disagreement is the expected
state.

---

## What it is for

Six things the tree does that only an instrument can confirm:

| | What is claimed | What has to be seen |
|---|---|---|
| DShot | 300 and 600 kbit/s, bit 0 is 37% high and bit 1 is 75% | the actual high time of each bit, against an ESC's tolerance rather than the specification |
| Bidirectional DShot | the ESC replies at five quarters of the wire rate after a turnaround | the turnaround delay, and whether 30 µs is what a real ESC waits |
| PPM (pulse position modulation) | 8 channels in a 25,000 µs frame at 40 Hz, direct memory access ring | the frame geometry with the ring running, and the gap between repeats |
| Servo PWM (pulse-width modulation) | 50 Hz, 500 to 2500 µs, 1 µs resolution | the frame period, the pulse at both ends of travel, and the jitter |
| SBUS | inverted serial, 100 kbit/s, 25 bytes | the framing, and what the panel makes of a receiver in failsafe |
| OpenYGE | telemetry frames and parameter access | the frame timing and the reply latency of a real ESC |

And the programmer's four ESC protocols and one servo protocol, which the
panel can drive but which no test exercises against a device: BLHeli_S and
AM32 (one-wire bootloader, 19,200 baud), ESCape32 (text command line over the
signal line), VESC (framed packets, 115,200 baud) and Hitec D-series.

---

## The parts

**Raspberry Pi 5** — the host. It runs `sigrok-cli` for capture, OpenOCD for
flashing, and the run scripts in `host/`. It is the only thing on this bench
with a network address, and it is what an agent drives.

**Kingst LA2016** — 16 channels, passive. Supported by libsigrok's
`kingst-la2016` driver, which needs the field-programmable gate array (FPGA)
bitstream extracted from the vendor's software: that extraction is a setup
step, because without it the analyser enumerates and captures nothing.

**Two separate gates, in this order: the driver, then the bitstream.** Neither
substitutes for the other, and a bench that has cleared one has cleared one.

The driver is not in libsigrok 0.5.2, which is the current release and what
Debian 13 packages: `sigrok-cli --list-supported` on the bench host lists 162
drivers and no `kingst-la2016`, and naming it prints
`Driver kingst-la2016 not found.` So the packaged `sigrok-cli` cannot address
this analyser at all, and the bench needs libsigrok built from git.

It is built on this host, from pinned commits, into `/opt/sigrok`. The
commands are under *Building it* below. Building here rather than elsewhere is
the short path: the packages are one `apt-get`, the result matches this host's
ABI (application binary interface) because it was compiled against it, and
there is no artifact to publish, fetch or verify in between.

**Built on the bench host.** libsigrok
`0bc2487778e660f4d3116729b6f4aee2b1996bb0` of 2025-11-20 and sigrok-cli
`f44dd91347e7ac797cefc23162b9fcf0b7329f1f` of 2024-08-26, giving libsigrok
0.6.0-git and sigrok-cli 0.8.0-git against Debian 13.6. Commits rather than a
branch: master carries no release tag and no promise about its interface, so
a measurement recorded against "master" cannot be reproduced.

**The build runs beside the distribution's sigrok, and the analyser
captures.** Measured on the bench host on 2026-09-09, with the distribution's
`sigrok-cli` 0.7.2 and `libsigrok4t64` 0.5.2 installed throughout.

| | |
| --- | --- |
| `ldd /opt/sigrok/bin/sigrok-cli` | `libsigrok.so.4 => /opt/sigrok/lib/libsigrok.so.4` |
| scan | `kingst-la2016:conn=1.5 - Kingst LA2016 with 18 channels: CH0..CH15 PWM1 PWM2` |
| capture | 948 lines from 1000 samples at 1 MHz |

The runpath decides against Debian's library on the default search path, which
is the whole reason for the `/opt` prefix.

**The FX2 firmware upload is confirmed at the USB descriptor level rather than
inferred.** Before a scan the analyser reports `iManufacturer 0` and
`iProduct 0`, the un-programmed FX2. After one it reports `iManufacturer 1
Kingst` and `iProduct 2 Kingst Logic Analyzer`. So the blobs extracted from
KingstVIS v3.6.6 are what the driver uploaded, and the CRC-32 cross-check
against the values the extractor's man page documents for v3.5.0 holds as far
as a working instrument.

Two readings that are not faults and are not explained here:

A capture returns more than it is asked for. 1000 samples at 1 MHz returned
3746 bits per channel across 59 lines. Where the rounding happens is not
chased. `host/selftest.sh` asks only for at least one line, so this has never
mattered to it; a recipe that assumes it gets the sample count it asked for
would be wrong.

**The loop is closed and the analyser measures.** A jumper from `PWM1` to
`CH0` uses the device's own generator as the source. Sampling at 200 MHz,
4,000,000 samples per run:

| Commanded | Measured | Error | Duty | Cycles |
| --- | --- | --- | --- | ---: |
| 1 kHz | 1000.0 Hz | +0.000% | 50.00% | 19 |
| 10 kHz | 10000.0 Hz | +0.000% | 50.00% | 199 |
| 100 kHz | 100000.0 Hz | -0.000% | 50.00% | 2000 |
| 1 MHz | 1000000.0 Hz | +0.000% | 50.00% | 20008 |

**Those figures are against the device's own timebase and nothing else.** The
period standard deviation is 0.0 ns on every run, and that is a tell rather
than a quality figure: the generator and the sample clock come off the same
FPGA oscillator, so every period lands on an exact number of samples --
1 kHz is precisely 200,000 samples at 200 MHz. What is proven is the capture
path, the timing arithmetic and the duty resolution. A wrong crystal cannot be
detected this way, because both halves would be wrong together. Absolute
accuracy is untested; it needs an external reference and none has been
applied. This is the same trap as a decoder written from the specification the
firmware was written from: agreement for the same wrong reason.

Duty cycle at 100 kHz, which is the half a self-agreeing clock cannot
manufacture -- a ratio does not depend on the timebase being right:

| Commanded | Measured | High |
| --- | --- | --- |
| 10% | 10.00% | 1000 ns of 10000 ns |
| 25% | 25.00% | 2500 ns |
| 50% | 50.00% | 5000 ns |
| 75% | 75.00% | 7500 ns |
| 90% | 90.00% | 9000 ns |

Two things about driving `PWM1` from `sigrok-cli`, both of which present as
absent hardware:

`--channel-group PWM1` makes `samplerate` "not applicable", so one invocation
cannot both set the stimulus and choose a sample rate, and the capture then
runs at the 200 MHz default without saying so. Setting 1 kHz and capturing a
0.51 ms window yields one edge on `CH0`, which reads as a dead output and is
half a period of the default.

The `PWM1` configuration does not survive the invocation. `--set` followed by
a separate capture reads back `enabled` off and `output_frequency` 1000.
Stimulus and capture have to be one command.

The analyser has measured a signal it generated itself. It has not measured
anything the coprocessor produced, which is what this bench exists for.

Clearing that gate means the driver exists. It says nothing about whether the
analyser captures: the FX2 microcontroller firmware and the FPGA bitstreams
are Kingst material with no redistribution grant and are not in this
repository. They are extracted from the vendor software on the bench with
`sigrok-fwextract-kingst-la2016`, a Python script from
sigrok-util that needs no build. The vendor's download page is `/en/download`.
KingstVIS v3.6.6 yields 18 files rather than the five the extractor's man page
documents against v3.5.0, and all 18 are installed because which one an
analyser needs is decided at scan time from two EEPROM bytes. The five the man
page documents come out of v3.6.6 with identical sizes and CRC-32 values.
Installed to `/usr/share/sigrok-firmware`, which is in the default
`XDG_DATA_DIRS`, so a libsigrok under `/opt/sigrok` finds them without
`SIGROK_FIRMWARE_DIR` being set. `host/selftest.sh` reports the
two gates separately -- it asks the library what drivers it carries, then asks the
instrument for samples -- so a missing driver reads as a missing driver, a
missing bitstream as a capture that returns nothing, and neither as an
unplugged instrument.

**The prefix is `/opt/sigrok`, not `/usr/local`.** Debian's `libsigrok4t64`
carries the soname `libsigrok.so.4` and so does the git build. With both on
the default search path the loader picks by path order, and neither end
reports a mismatch -- a Pi with the Debian package installed would quietly run
0.5.2 and find no analyser. Under `/opt`, with a runpath to `/opt/sigrok/lib`
linked into `sigrok-cli`, the two cannot meet, and the distribution's sigrok
keeps working untouched.

Building it:

    sudo apt-get install -y --no-install-recommends \
        build-essential git ca-certificates \
        autoconf automake libtool pkg-config autoconf-archive \
        libglib2.0-dev libzip-dev zlib1g-dev libusb-1.0-0-dev \
        libsigrokdecode-dev
    git clone https://github.com/sigrokproject/libsigrok.git
    git -C libsigrok checkout --detach 0bc2487778e660f4d3116729b6f4aee2b1996bb0
    ( cd libsigrok && ./autogen.sh \
        && ./configure --prefix=/opt/sigrok --disable-bindings \
                       --enable-kingst-la2016 \
        && make -j"$(nproc)" && sudo make install )
    git clone https://github.com/sigrokproject/sigrok-cli.git
    git -C sigrok-cli checkout --detach f44dd91347e7ac797cefc23162b9fcf0b7329f1f
    ( cd sigrok-cli && ./autogen.sh \
        && PKG_CONFIG_PATH=/opt/sigrok/lib/pkgconfig ./configure \
             --prefix=/opt/sigrok LDFLAGS=-Wl,-rpath,/opt/sigrok/lib \
        && make -j"$(nproc)" && sudo make install )

The sequence takes about 82 seconds on the bench host: 10 s for the packages,
72 s cumulative through libsigrok, 82 s through sigrok-cli.

**The package list is not tested on a host that has never had sigrok on it.**
The sequence above has been walked on the bench host, which already carried
the runtime libraries and both udev rules, so what it proves is the compile,
the configure flags, the driver being built in and the runpath -- the
configure summary prints `kingst-la2016................... yes` and the built
library lists the driver. Whether a first-time follower meets a package this
list omits is unknown, and finding out needs a machine that has never had
sigrok installed.

`--enable-kingst-la2016` explicitly: the default is a check, under which
configure records `kingst-la2016 no (missing: libusb)` in its summary and the
build succeeds without the driver. With the flag a missing dependency is an
error instead. `--disable-bindings` switches off the C++, Python, Ruby and
Java bindings in one flag; nothing on the bench imports libsigrok.

Then the rules, the group and the path:

    sudo install -m 644 libsigrok/contrib/60-libsigrok.rules \
                        libsigrok/contrib/61-libsigrok-plugdev.rules \
                        /etc/udev/rules.d/
    sudo udevadm control --reload
    sudo udevadm trigger --subsystem-match=usb
    sudo adduser "$USER" plugdev        # log out and back in to take effect
    export PATH=/opt/sigrok/bin:$PATH

**The udev rules are a step, and skipping them reads as no analyser.**
`make install` places none of them: they are in the source tree's `contrib`
only, which is why they are copied by hand above. Debian's libsigrok 0.5.2
ships rules that predate this driver
and carry no entry for `77a1`, so the LA2016 gets no `ID_SIGROK` tag and
neither the `uaccess` nor the `plugdev` rule fires. Measured on the bench
host: the device node stays `crw-rw-r-- root root`, read-only for the
operator, against a driver that has to write to it to upload the FX2
firmware. With the shipped rules installed, reloaded and triggered, the node
is `root:plugdev` with `uaccess` and opening it `O_RDWR` succeeds; no replug
is needed. Without them a scan as a normal user returns no devices, which is
the same reading as an unplugged instrument. Both files
are needed and the numbers matter: `60-` tags the device and `61-` acts on the
tag, so the order they sort in is what makes them work.

The `plugdev` rule rather than the `uaccess` one, because `uaccess` grants
access through systemd-logind to a user on a local seat and an SSH session has
no seat. The bench is driven over SSH.

**The `PATH` line is a step, not a suggestion.** The distribution's
`sigrok-cli` is at `/usr/bin` and wins until `/opt/sigrok/bin` is put ahead of
it, so between unpacking and that line the bench is still running 0.5.2. Make
it permanent wherever this host keeps its environment; a shell that has not
had it is a shell that measures with the wrong binary.

**Which build is installed is recorded, and checked.** `host/selftest.sh`
reads a manifest at `/opt/sigrok/MANIFEST.txt`. Plain text, machine-readable
keys first, one per line, colon-separated: `libsigrok-commit`,
`sigrok-cli-commit`, `libsigrok-version`, `sigrok-cli-version`,
`libsigrok-sha256`, `sigrok-cli-sha256`, `debian-version`, `prefix`,
`built-utc`. Write it after installing, from the files that were installed:

    sudo tee /opt/sigrok/MANIFEST.txt >/dev/null <<EOF
    libsigrok-commit: $(git -C libsigrok rev-parse HEAD)
    sigrok-cli-commit: $(git -C sigrok-cli rev-parse HEAD)
    libsigrok-version: $(/opt/sigrok/bin/sigrok-cli --version | sed -n 's/.*rt: \([^ )]*\).*/\1/p' | head -1)
    sigrok-cli-version: $(/opt/sigrok/bin/sigrok-cli --version | head -1 | awk '{print $2}')
    libsigrok-sha256: $(sha256sum /opt/sigrok/lib/libsigrok.so.4 | cut -d' ' -f1)
    sigrok-cli-sha256: $(sha256sum /opt/sigrok/bin/sigrok-cli | cut -d' ' -f1)
    debian-version: $(cat /etc/debian_version)
    prefix: /opt/sigrok
    built-utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)
    EOF

The versions are read out of the binary rather than composed from a tag, so
the recorded string is what the binary prints. The hashes are taken from the
installed files rather than from the build tree: anything that strips or
re-links between the two would otherwise record one object and run another.

**The manifest belongs to one installation.** Two builds of the same commits
with the same flags do not produce the same bytes -- build paths, timestamps
and a different compiler all reach the binary -- so no two benches record the
same hashes. Measured: a second build of `0bc24877` and `f44dd91` on this host
gave a `sigrok-cli` of `8be9ed02c5595b68...` against the `6c8febc235aec1de...`
of another build of the same commits. That is why the manifest is written from
the installed files after installing, and it is why a hash mismatch between two
correct benches must not be settled by copying one bench's manifest to the
other. Doing that makes the check pass for a build it does not describe, which
is the one failure it exists to catch. Build the manifest where the build is.

**The hashes are what decide.** A version string is not a build: two commits
can carry one, so a comparison against `sigrok-cli-version` passes on a stale
manifest and then attributes a commit to a library that did not produce the
captures. `host/selftest.sh` compares the sha256 of the binary on `PATH` and
of the libsigrok that is actually loaded -- resolved with `ldd`, because the
library the dynamic linker finds need not be the one beside the binary. The
version lines are printed as labels and nothing turns on them.

Five readings come out of it:

| What the selftest says | What it means |
|---|---|
| `libsigrok recorded: <version> at <commit>` | the manifest is there and names a build |
| `sigrok provenance: unrecorded` | no manifest at that path, so which build is on `PATH` is unanswered. A distribution `sigrok-cli` reads as this |
| `sigrok provenance: missing keys` | a manifest that cannot decide anything. Without it a truncated file passes, because an absent recorded hash equals an absent parsed one |
| `sigrok-cli on PATH: <path>, not the recorded <prefix>/bin/sigrok-cli -- put <prefix>/bin ahead of it on PATH` | the build is installed and the `PATH` step has not been done. This is the expected state in between, not a broken install |
| `libsigrok loaded: <path> is not the recorded build` | the binary is right and the library under it is not, which is the reading a stale manifest or a half-finished install gives |

The fourth is why the distribution `sigrok-cli` is left installed rather than
removed. A bench that only works once a package is gone is a bench that breaks
on the next machine, so the two are made to coexist -- the prefix and the
runpath keep them apart, and the `PATH` step decides which one a shell gets.
The selftest names the step in the reading, so an operator who meets it
between the two commands above sees a procedure that is not finished rather
than a fault. `SIGROK_MANIFEST` overrides the path.

The hashes are taken from the installed files, not from the build tree.
Hashing what was built rather than what runs would record one object and
install another if anything strips or re-links between the two, and the bench
would report a shadow on a correct install.

**RP2350 board** — the same part as the bench coprocessor, in one of two roles
per run:

- **Stimulus.** Its programmable input/output (PIO) blocks generate protocol
  traffic that is known exactly, because the generator states it. A capture of
  known traffic is what tells you whether a decoder is right, which has to
  come before any capture of the bench itself is worth reading.
- **Responder.** It pretends to be the device at the other end — an ESC that
  answers a BLHeli_S bootloader handshake, or one that emits OpenYGE
  telemetry, or a servo that answers Hitec D-series. That makes the panel's
  programmer and telemetry paths testable without owning every device. It also
  answers as a touch controller at its own address; see below.
- **Hands.** It closes the relays across both boards' reset and boot buttons,
  which is what makes a cold start scriptable and a bad flash recoverable.

**A camera** on a fixed mount, looking at the display. It is the only thing
here that can see what the panel actually shows, as opposed to what its
renderer would draw.

**The bench itself** — panel, display, touch and the CAN (controller area
network) controller — lives on the rig rather than being attached per session.
That is what makes a regression run possible: the same recipes over the same
wiring after every merge, rather than a measurement somebody remembered to
take.

Having the display and the CAN controller wired changes what can be measured,
in three ways:

- **The panel runs as it ships.** The renderer is drawing while the control
  task arms, stops and drives, which is the condition the frame budget and the
  PSRAM (pseudo-static random-access memory) bandwidth concern in `STATUS.md`
  are about. A timing measured with the screen dark is not the bench's timing.
- **The link is observable at both ends.** Four probes on the XL2515's serial
  peripheral interface (SPI) show every register the driver touches, and a
  fifth on RXCAN between the controller and the transceiver shows the bit
  stream itself. Both are needed: the SPI says when the driver noticed a
  frame, RXCAN says when it arrived, and the difference between those two is
  the polling interval. The controller's INT pin is not a probe point --
  `firmware/iomcu/src/xl2515.c` writes zero to CANINTE and polls CANINTF, so
  INT never asserts. With RXCAN, the 1,000 ms host timeout and the 200 ms
  silence watchdog stop being numbers only the code believes.
- **Touch becomes something a script can do.** See below; it is the one that
  matters most.

---

## Wiring

One star ground. Every ground in the table below returns to the same point,
and the analyser's ground is part of that star rather than a daisy chain from
the far end of it.

Proposed channel map, to be checked against the wiring as built:

| Channel | Signal | Source | Note |
|---|---|---|---|
| 0 | GP0 output | coprocessor | whatever OUTPUTS binds first |
| 1 | GP1 output | coprocessor | |
| 2 | GP2 output | coprocessor | the servo screen's own pin |
| 3 | heartbeat | panel J8 / GPIO6 to coprocessor GP3 | 20 ms square, the safety line |
| 4 | CAN SCK | coprocessor GP10, pad 14 | the XL2515's SPI clock |
| 5 | CAN MOSI | coprocessor GP11, pad 15 | |
| 6 | CAN MISO | coprocessor GP12, pad 16 | with SCK and MOSI, every register the driver writes |
| 7 | CAN CS | coprocessor GP9, pad 12 | frames the transaction, and is the cheapest thing to trigger on |
| 8 | RXCAN | XL2515 to transceiver | the bit stream itself, which is when a frame actually arrived |
| 9 | touch SCL | panel's I²C bus | the clock, shared by the real controller at 0x5D and the emulator at 0x14 |
| 10 | touch SDA | panel's I²C bus | with SCL, which address answered and what it said |
| 11 | DShot reply | ESC to coprocessor | the same wire as channel 0 when bidirectional; kept separate so a probe can sit on the ESC end |
| 12 | programmer line | one-wire, 19,200 baud | BLHeli_S, AM32, ESCape32 |
| 13 | telemetry RX | ESC to bench | OpenYGE, and DShot extended telemetry |
| 14 | SBUS | receiver to bench | inverted; the analyser reads it as it is and the decoder inverts |
| 15 | TXCAN, or spare | XL2515 to transceiver | the other direction when a run wants both ends of an exchange |

Not on the analyser: the relay contacts across both boards' reset and boot
buttons, closed by the RP2350. They are wiring all the same and belong on the
same star ground.

**The budget is 16 channels and the map is 15**, so a run takes the subset it
needs rather than everything at once: an output measurement wants channels 0
to 3, a link measurement wants 4 to 8, and only a failure that crosses the two
wants both.

**Levels.** The RP2350 runs at 3.3 V and its pins are not 5 V tolerant.
Servo and ESC signal lines are commonly 5 V, and a one-wire programming line
idles high through a pull-up at whatever the device runs at. Anything crossing
between the 3.3 V island and a 5 V device goes through a level shifter, and
every probe point gets a series resistor so a mistake costs a resistor. The
analyser's threshold is set per run rather than left where the last run put
it.

---

## Safety

**Nothing on this bench spins.** The loads are servos and an ESC that is
listened to rather than driven into a motor, so the hazards here are a shorted
rail and a board nobody can restart, not a propeller.

- The bench supply has a current limit set low enough that a short trips it
  rather than burning a track.
- The analyser is passive and stays passive: it observes, and nothing on it
  drives a line.
- Every probe point has a series resistor, so a mistake costs a resistor.

---

## Flashing without hands

The RP2350's BOOTSEL button is not available to an agent, so the board is
flashed over SWD (serial wire debug) rather than by unplugging it, driven from
the Pi's own pins through OpenOCD's `linuxgpiod` interface.

**Untested end to end.** No RP2350 has been on these pins. The commands below
run and are checked against the software installed on the bench host; what
none of them has done is find a target.

On a Raspberry Pi 5 the general-purpose input/output pins sit behind the RP1
controller, so OpenOCD's older native Broadcom driver does not work and the
character device is the route. Which device the 40-pin header is depends on
the kernel and the firmware, so it is read rather than assumed. On the bench
host -- Raspberry Pi 5 Model B Rev 1.1, kernel 6.18.34, Debian 13 --
`gpiodetect` reports the header as `gpiochip0 [pinctrl-rp1]`, 54 lines.

The OpenOCD on the bench host ships no `interface/linuxgpiod.cfg`. The driver
is selected by name:

    gpiodetect                       # which chip carries the header
    export SWD_GPIOCHIP=0 SWD_SWCLK=25 SWD_SWDIO=8
    openocd -c "adapter driver linuxgpiod" \
            -c "adapter gpio swclk -chip $SWD_GPIOCHIP $SWD_SWCLK" \
            -c "adapter gpio swdio -chip $SWD_GPIOCHIP $SWD_SWDIO" \
            -f target/rp2350.cfg

The driver is named before the GPIO assignments and `target/rp2350.cfg` after
them, because the target file selects the transport. Checked against OpenOCD
0.12.0+dev-snapshot (2026-02-16-16:07) on the bench host, which ships
`target/rp2350.cfg` and carries the `linuxgpiod` driver but no
`interface/linuxgpiod.cfg`.

With `-c "init; exit"` appended and no target on the pins it reaches the
driver and stops there:

    Info : Linux GPIOD JTAG/SWD bitbang driver
    Error: Error connecting DP: cannot read IDR

That is the whole of what has been confirmed: it is the reading an empty
header gives. It is not a diagnosis. A target that is present but miswired --
the two leads swapped, one of them off, the ground not on the star, the wrong
chip number -- gives the same error, so it separates "OpenOCD reached the
pins" from "something answered" and nothing finer. A board on the pins has to
replace it.

That build also ships `interface/raspberrypi5-gpiod.cfg`, which resolves the
chip number from the `/proc/device-tree/aliases` entry pointing at the RP1
instead of taking it from a variable. It puts SWDIO on GPIO8 and SWCLK on
GPIO11; this bench agrees on SWDIO and keeps SWCLK on GPIO25.

SWCLK is GPIO25 on header pin 22, SWDIO is GPIO8 on header pin 24, and ground
is pin 20. `testbench/WIRING.md` has the table. Any two free header GPIOs
would work, since the lines are bit-banged rather than driven by a peripheral,
but a bench is reproducible only if every assembler uses the same two.

SWDIO is on GPIO8 for its default pull. **Power-on values, valid only on a
line nothing has driven since boot** -- `pinctrl get 8,24,25` on the bench
host:

     8: no    pu | -- // GPIO8 = none
    24: no    pd | -- // GPIO24 = none
    25: no    pd | -- // GPIO25 = none

`pu` on GPIO8 against `pd` on GPIO24 and GPIO25. Read across the whole header
range on the same host, `pinctrl get 0-27` gives `pu` on GPIO0 to GPIO8 and
`pd` on GPIO9 to GPIO27, so the split is at GPIO9 and the three lines above
are not special cases.

The SWD specification puts a pull-up on SWDIO at the target, so GPIO8 is the
line that agrees with it, and OpenOCD's Pi 5 configuration puts SWDIO there
for the same reason.

**The condition is not decoration.** OpenOCD leaves the lines it drove as
inputs with no pull, so on a host where the check above has run, `pinctrl`
reports `pn` for GPIO8 and GPIO25 and not the values printed here. Reboot
before reading a pull, or read a line the bench does not touch.
`host/selftest.sh` prints the pull it finds for the two configured lines, with
the power-on value and the host's uptime beside it, so a `pn` reads as a line
that has been driven rather than as a contradiction of this table.

**The choice is not a fix for an observed fault.** No target has been on
either pin, so whether a pull-down on GPIO24 would have cost anything against
the RP2350's own termination is unknown. Nothing is wired yet, which is the
whole reason to spend a documentation change now rather than a rewiring later.

**GPIO8 is SPI0 CE0.** With SPI enabled on the header the `spidev` driver
claims the line and `linuxgpiod` cannot have it. SPI is off on the bench host
-- no `/dev/spidev*`, no `dtparam=spi` in `/boot/firmware/config.txt` -- and
stays off while SWD is on this pin. SWCLK on GPIO25 carries no such
attachment.

`host/selftest.sh` reads the same three variables, so the wiring is stated
once and nothing in the scripts has to know it.

Three wires: SWCLK, SWDIO and ground, and nothing else. The Pi's 3.3 V header
pin is a regulated output rather than a reference input, so wiring it to a
target that has its own supply ties two regulators together and back-powers
one of them. Both boards here are powered already; the Pi contributes the two
signals and the common ground.

`linuxgpiod` drives the two signals at the Pi's own 3.3 V, which is what the
RP2350 expects. A target at another voltage needs a level translator that
senses the target's rail, not a wire from this header.

There is no clock to set. OpenOCD prints `Note: The adapter "linuxgpiod"
doesn't support configurable speed` when the driver initialises, so an
`adapter speed` line is accepted and has no effect. Its
`interface/raspberrypi5-gpiod.cfg` puts the fixed rate at about 800 kHz for
SWD writes and 360 kHz for reads; neither is measured on this host.

A fixed rate is not the same as a suitable one, and nothing here has driven a
target to find out. If a flash fails to verify, `adapter speed` is not the
knob -- the PCIe policy below and a different adapter are the two things that
can move the timing.

Two host settings bear on it, both read on the bench host:

- PCIe active-state power management reads `powersave` on the bench host.
  That is a current value, not a fixed one: it is what boot leaves unless
  somebody sets it, and setting it does not survive a reboot.
  `host/selftest.sh` prints the policy it finds. OpenOCD's Pi 5 configuration
  warns that under anything but `performance` the first few pulses are clocked
  as fast as 20 MHz, and asks for:

      echo performance | sudo tee /sys/module/pcie_aspm/parameters/policy

- `libgpiod` is 2.2.1, so the option spellings in `WIRING.md` are the version 2
  ones.

The same applies to the bench's own coprocessor if it is to be reflashed
between runs.

---

## Touch, which is the one that matters

Fifty-one review findings on one branch were about arming, and every one was
argued rather than measured, because the panel's control task is outside the
host suite and a gesture needs a finger. The cases were: a press held for two
seconds, a finger that slides off the button, a second contact landing on it,
a stop pressed while a hold is running, and touch that stops answering
altogether. All of them are decided by what the GT911 touch controller reports
over I²C (inter-integrated circuit).

**So the RP2350 pretends to be the GT911, at an address of its own.**

Sharing an address does not work: two devices both acknowledge and their
open-drain pulls combine, so what the panel reads is neither, and the panel
probes the physical part at start-up anyway --
`firmware/panel/components/gt911/gt911.c` tries 0x5D and then 0x14.

Giving the emulator the second of those two addresses removes the conflict
entirely. The real controller answers at 0x5D as it always did, the emulator
answers at 0x14, and nothing on the bus collides. What changes is which one
the panel asks first: it probes the debug address, uses it if something is
there, and falls back to the real controller when nothing answers. A panel
with no bench attached finds nothing at 0x14 and behaves exactly as it does
now, one failed probe later.

**The panel does not do this yet.** `firmware/panel/components/gt911/touch.c`
passes a zeroed configuration, so `gt911.c` tries 0x5D first, finds the real
controller and never asks 0x14. Until that changes the emulator can sit on the
bus answering perfectly and the panel will not have spoken to it: every
gesture and every silence in the table below is a plan, not a capability.

What the change is: `gt911_new()` already takes an address to try
(`cfg.i2c_addr`), so it is which candidate goes first and where that choice
comes from -- a build option, off in a release, so a shipped panel does not
look for a debug device at all. It is one small change to the panel and it
belongs in its own pull request, with its own review, rather than inside a
description of a bench.

**And the panel says so while it is doing it.** A bench being driven by
something other than the panel's own glass is a fact the operator must be able
to see, in the same way a run that is not being recorded says so. `touch.h`
already reports which address answered, so the band has what it needs.

With it built, a recipe says "press at (612, 396), hold 2.1 s, release" and
the panel cannot tell the difference. With the
analyser on the heartbeat and the output pin at the same time, one capture
holds the gesture and what the bench did about it:

| Gesture the recipe drives | What the capture has to show |
|---|---|
| hold 2.1 s on ARM | the output starts driving, and not before 2.0 s |
| hold 1.9 s and release | nothing drives |
| hold, slide off the button, hold | nothing drives |
| second contact lands, first leaves | nothing drives |
| STOP while a hold is running | the heartbeat stops being asserted within 150 ms |
| the emulator stops answering | the bank comes down, and the far end fails safe |

That last one is the case that took four review rounds to get right and still
has no test. Here it is a wire going quiet on purpose.

It also gives the failure that cannot be staged any other way: a touch
controller that answers slowly, or that reports a contact that was never
there. Both are things a real GT911 does when its ground is poor, and neither
has ever been in front of this firmware.

---

## A camera on the display

The panel's own renderer is checked on the host: `tools/render_ui.py` draws
every screen and `--check` fails on a pixel of drift. What that cannot see is
the panel — a screen that is blank, torn, shifted, or the wrong colours,
which is what `STATUS.md` warns about when a second core touches PSRAM while
the bounce buffer is refilling.

A camera on a fixed mount closes that. The comparison is not naive: a
photograph has perspective, lighting and a lens, so a run rectifies before it
compares. The splash screen is the calibration frame — it is drawn from
constants, it fills the panel, and it has corners. One capture of it per
session gives the transform; after that a photograph can be laid over the
golden render.

What it is asked has to match what it can answer. Geometry to a few pixels,
yes: is the ARM button where it should be, is the band drawn, has the screen
shifted. Colour to a tolerance and no tighter, because a camera's white
balance is not the panel's. Text by its shape rather than by reading it,
unless the run wants to install a reader and prove that too.

It also gives the plainest evidence there is for a gesture: the recipe drives
a two-second hold, the analyser shows the pin start driving, and the
photograph shows ARMED on the band. Three independent things saying the same
thing is what makes a measurement believable.

---

## Reset, and the boot buttons

Relays across the buttons, one contact each, closed by the RP2350. A relay
contact is what a button is -- a dry contact to ground -- so there is no line
to drive, no level to match, and nothing that can hold a pin high. Coil
unpowered is button not pressed, which is the state a board runs in, so a
controller that has crashed or lost power leaves both boards alone.

What that makes possible:

- **A cold start on demand.** The link-silent fault, the 1,200 ms splash, the
  CAN self-test and the heartbeat's first acquisition all happen once per
  power-up and have never been seen across a hundred of them. A reset under a
  script turns that into a loop.
- **Boot mode without hands.** BOOT closed, RESET pulsed, RESET released, BOOT
  released: that sequence is what puts each board in its loader. The panel
  needs it -- it is flashed over a serial port with BOOT held, and without this
  there is no unattended way to put firmware on the panel at all.
- **Recovery.** A flash that leaves a board unable to run is undone by the same
  two contacts rather than by somebody driving to the bench.

Two practical notes. Use signal relays with gold-plated contacts: a button
line switches microamps, and contacts rated for power can grow an oxide film
that a dry circuit will not break through. And run the coils from their own
supply through a transistor with a flyback diode -- a coil collapsing into the
rail that feeds the panel is a brown-out that reads like a firmware fault.

The chain then has no dead end: the Pi resets the RP2350 over SWD, the RP2350
closes the contacts that reset the boards.

---

## How a run works

A run is a recipe, a capture, a decode and a verdict, in that order, and it
leaves an artefact behind.

1. **Recipe** — a file that says what the stimulus does, what to capture
   (channels, sample rate, trigger, duration), which decoder to run, and what
   the assertion is. It is committed; the numbers it produces are not asserted
   by hand anywhere else.
2. **Capture** — `sigrok-cli` writes a `.sr` file into `captures/`. Captures
   are large and are not committed, except a small reference set that the
   decoders are tested against.
3. **Decode** — a protocol decoder turns the capture into rows: one per bit,
   frame or pulse, with times in microseconds.
4. **Verdict** — the recipe's assertion, printed as a number and a unit, not
   as a pass. "37.2% high at 300 kbit/s, tolerance ±5%" is a result; "OK" is
   not.

The artefact is the capture plus the decoded rows plus the verdict. A claim in
`docs/` that this bench has measured says which run measured it.

---

## Trusting a decoder before trusting a measurement

sigrok has no DShot decoder. One has to be written, and a decoder written from
the same specification as the firmware would agree with the firmware for the
same wrong reason.

The first check is a round trip against the tree's own builder:
`shared/dshot/dshot_frame.c` makes a frame from a value and a cyclic
redundancy check, the host suite emits it as a waveform, and the decoder must
read back what went in. It needs no hardware and can be done before the bench
exists.

**That check is necessary and it is not sufficient.** The production path
calls the same builder -- `firmware/iomcu/src/out_dshot.c` -- so decoder and
firmware can agree on a bit order or a checksum convention that is wrong in
the same way, and the round trip would pass. It proves transcription, not
convention.

Convention needs a source outside this tree, and there are three, in order of
what they cost:

1. **Published vectors** — a throttle value and the frame it must produce,
   from the protocol's own documentation rather than from an implementation.
2. **A device that is not ours.** An ESC that spins at the commanded throttle
   is evidence the frames are right, because it was written by somebody who
   never read this code. A reply decoded to an electrical revolutions figure
   that tracks a separately measured shaft speed is the same evidence for the
   reply direction.
3. **A capture of another implementation** driving the same ESC, decoded by
   this decoder and compared.

Until one of those has been done, a capture of the coprocessor says the
firmware and the decoder agree, and no more than that.

---

## What is here

    testbench/
      README.md      this: what the bench is for, and why each part is there
      WIRING.md      what to connect, in order, with a check after each step
      host/          the scripts the Pi runs: capture, decode, self-test
      decoders/      protocol decoders, and their offline verification
      captures/      run artefacts, not committed; reference/ is

---

## What is not built yet

- **The panel's debug touch address.** Described above; without it the touch
  emulator injects nothing. One build option and one probe order, in its own
  pull request.
- **The decoders.** Described in `decoders/README.md`. Two of their three
  checks wait on nothing: the round trip against this tree's own builder, and
  the published vectors that prove the convention. Only the comparison against
  a capture of another implementation needs the bench.
- **The recipes.** The runner, and the first measurement, wait on the bench
  existing.

---

## Settled

- **The analyser's bitstream is provided**, and it is not redistributed:
  it is Kingst material with no grant, so it is extracted from the vendor
  software onto the bench rather than shipped with the build.
  `host/selftest.sh` still asks for it, because the failure is silent -- an
  analyser without it enumerates, accepts a capture and returns nothing, which
  reads as a quiet bench rather than a broken one. Having it settles the
  second of the two gates under *The parts*, never the first.
- **The driver is built on the bench host, to `/opt/sigrok`**, from pinned
  commits, and what was built is recorded in `/opt/sigrok/MANIFEST.txt`. The
  prefix and the runpath are what keep it away from the distribution's
  `libsigrok.so.4`, which carries the same soname.
- **SWD over `linuxgpiod`**, three wires and no supply between the Pi and a
  board that has its own.
- **The whole bench lives on the rig** — panel, display, touch and the CAN
  controller — so a regression run is the same recipes over the same wiring
  after every merge.
- **Relays across BOOT and RESET**, closed by the RP2350.
- **A camera on the display**, on a fixed mount, calibrated from the splash.
- **The touch emulator answers at 0x14**, beside the real controller at 0x5D.
