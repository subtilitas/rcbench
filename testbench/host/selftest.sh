#!/usr/bin/env bash
#
# Does the bench exist?
#
# Run this before believing any measurement, and after any change to the
# wiring.  It asks four questions and answers each with what it found rather
# than with a pass: an analyser that enumerates but has no bitstream captures
# nothing, and that failure looks like a quiet bench.
#
# SPDX-License-Identifier: MIT
set -uo pipefail

fail=0
say() { printf '%-28s %s\n' "$1" "$2"; }
bad() { say "$1" "$2"; fail=1; }

# 1. The analyser, and whether libsigrok can actually drive it.
if ! command -v sigrok-cli >/dev/null; then
    bad "sigrok-cli" "not installed"
else
    say "sigrok-cli" "$(sigrok-cli --version | head -1)"

    # Which sigrok is this?  The bench runs a libsigrok built from git,
    # because the released 0.5.2 carries no kingst-la2016 driver, and a
    # measurement read through an unnamed library version cannot be
    # reproduced.  The build records what it made in a manifest, and
    # unpacking the tarball into /usr/local puts it at the path below.  It is
    # data, not shell, so it is read with grep and never sourced.
    : "${SIGROK_MANIFEST:=/usr/local/share/doc/rcbench-sigrok/MANIFEST.txt}"
    manifest_get() {
        grep -m1 "^$1:" "$SIGROK_MANIFEST" 2>/dev/null |
            cut -d: -f2- | sed 's/^[[:space:]]*//'
    }

    if [ ! -r "$SIGROK_MANIFEST" ]; then
        # An apt-installed sigrok-cli reads as this.  It is not the same
        # fault as a missing driver: the driver may well be there and the
        # question of which build produced it is simply unanswered.
        bad "sigrok provenance" "unrecorded -- no manifest at $SIGROK_MANIFEST"
    else
        lib_version=$(manifest_get libsigrok-version)
        lib_commit=$(manifest_get libsigrok-commit)
        cli_version=$(manifest_get sigrok-cli-version)
        if [ -z "$lib_version" ] || [ -z "$lib_commit" ] || [ -z "$cli_version" ]; then
            bad "sigrok provenance" "manifest at $SIGROK_MANIFEST is missing keys"
        else
            say "libsigrok" "$lib_version at $lib_commit"
            # The apt package and the built one can both be on PATH.  What
            # runs is what the shell finds first, which need not be what the
            # manifest describes.
            on_path=$(sigrok-cli --version | head -1 | awk '{print $2}')
            if [ "$on_path" != "$cli_version" ]; then
                bad "sigrok-cli on PATH" \
                    "$on_path at $(command -v sigrok-cli), manifest records $cli_version"
            else
                say "sigrok-cli on PATH" "$on_path, matches the manifest"
            fi
        fi
    fi

    # Ask the library what it carries before asking it to scan. The
    # kingst-la2016 driver is not in libsigrok 0.5.2, which is the current
    # release and what Debian packages; naming a driver libsigrok does not
    # have prints "Driver kingst-la2016 not found." and reads exactly like an
    # analyser that is unplugged.
    if ! sigrok-cli --list-supported 2>/dev/null |
         grep -qE '^[[:space:]]+kingst-la2016[[:space:]]'; then
        bad "kingst-la2016 driver" "not in this libsigrok -- see README, The parts"
        scan=
    else
        scan=$(sigrok-cli --driver kingst-la2016 --scan 2>&1 | tail -n +2)
    fi
    if [ -z "$scan" ]; then
        bad "LA2016" "not found by the kingst-la2016 driver"
    else
        say "LA2016" "$(echo "$scan" | head -1)"
        # Enumerating is not the same as working: the driver needs the FPGA
        # bitstream from the vendor software, and without it a capture
        # returns nothing at all rather than an error.
        n=$(sigrok-cli --driver kingst-la2016 --samples 1000 --config samplerate=1m \
                       -O bits 2>/dev/null | wc -l)
        if [ "$n" -lt 1 ]; then
            bad "LA2016 capture" "enumerates but captured nothing -- check the FPGA bitstream"
        else
            say "LA2016 capture" "$n lines from 1000 samples at 1 MHz"
        fi
    fi
fi

# 2. SWD, which is what makes flashing unattended.
#
# The route is linuxgpiod: the Pi's own pins through the character device.
# Which device carries the 40-pin header depends on the kernel and the
# firmware, so it is named here rather than guessed, and the three settings
# come from the environment so this file does not have to know the wiring.
: "${SWD_GPIOCHIP:=}"
: "${SWD_SWCLK:=}"
: "${SWD_SWDIO:=}"

if ! command -v openocd >/dev/null; then
    bad "openocd" "not installed"
elif [ -z "$SWD_GPIOCHIP" ] || [ -z "$SWD_SWCLK" ] || [ -z "$SWD_SWDIO" ]; then
    say "openocd" "$(openocd --version 2>&1 | head -1)"
    bad "RP2350 over SWD" "set SWD_GPIOCHIP, SWD_SWCLK and SWD_SWDIO -- gpiodetect says which chip"
else
    say "openocd" "$(openocd --version 2>&1 | head -1)"
    # OpenOCD 0.12.0+dev-snapshot (2026-02-16-16:07), the build on the bench
    # host, ships no interface/linuxgpiod.cfg, so the driver is named.
    # target/rp2350.cfg comes after the GPIO assignments: it selects the
    # transport, which needs the pins already set. linuxgpiod has no
    # configurable speed, so nothing sets one. With no target on the pins
    # openocd exits 1 after "Error connecting DP: cannot read IDR".
    if openocd -c "adapter driver linuxgpiod" \
               -c "adapter gpio swclk -chip $SWD_GPIOCHIP $SWD_SWCLK" \
               -c "adapter gpio swdio -chip $SWD_GPIOCHIP $SWD_SWDIO" \
               -f target/rp2350.cfg \
               -c "init; exit" >/dev/null 2>&1; then
        say "RP2350 over SWD" "reachable on gpiochip$SWD_GPIOCHIP, clk $SWD_SWCLK, io $SWD_SWDIO"
    else
        bad "RP2350 over SWD" "not reachable on gpiochip$SWD_GPIOCHIP -- see README, Flashing without hands"
    fi
fi

# 3. Somewhere to put the artefacts.
here=$(cd "$(dirname "$0")/.." && pwd)
if [ -d "$here/captures" ] && [ -w "$here/captures" ]; then
    say "captures/" "writable"
else
    bad "captures/" "missing or not writable"
fi

# 4. The star ground, which nothing can check but a person.
say "star ground" "not checkable from here -- confirm by hand"

exit "$fail"
