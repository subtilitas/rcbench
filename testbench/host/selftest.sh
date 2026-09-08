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
    scan=$(sigrok-cli --driver kingst-la2016 --scan 2>&1 | tail -n +2)
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

# 2. The debug adapter, which is what makes flashing unattended.
if ! command -v openocd >/dev/null; then
    bad "openocd" "not installed"
else
    say "openocd" "$(openocd --version 2>&1 | head -1)"
    if openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
               -c "init; exit" >/dev/null 2>&1; then
        say "RP2350 over SWD" "reachable"
    else
        bad "RP2350 over SWD" "not reachable -- see README, Flashing without hands"
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
