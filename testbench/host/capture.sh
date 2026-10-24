#!/usr/bin/env bash
#
# One capture, into an artefact that says what it is.
#
#   capture.sh <name> <channels> <samplerate> <samples> <threshold> [trigger]
#
#   capture.sh dshot300-idle D0 24m 2m 1.65
#   capture.sh dshot300-arm  D0,D6 24m 4m 1.65 D0=r
#   capture.sh servo-5v      D2 8m 1m 2.5
#
# The threshold is in volts and is required, not defaulted: this bench has a
# 3.3V island next to level-shifted 5V devices, and a capture taken at the
# threshold the last run left behind is an artefact that reads plausibly and
# means nothing.  Halfway is the usual choice -- 1.65V for 3.3V logic, 2.5V
# for 5V.
#
# The sample rate is the first thing to get right: a DShot600 bit is about
# 1.67us, so 24MHz gives 40 samples a bit and a rate low enough to hold
# 16 channels.  Reading a 1us servo step needs no more than 8MHz; reading a
# bit edge does.
#
# Writes captures/<name>-<threshold>v-<timestamp>.sr and prints the path.
# Nothing here decodes: a capture is evidence and stays as it was taken.
#
# SPDX-License-Identifier: MIT
set -euo pipefail

if [ $# -lt 5 ]; then
    sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi

name=$1
channels=$2
rate=$3
samples=$4
threshold=$5
trigger=${6:-}

here=$(cd "$(dirname "$0")/.." && pwd)
out="$here/captures/${name}-${threshold}v-$(date -u +%Y%m%dT%H%M%SZ).sr"

args=(--driver kingst-la2016
      --config "samplerate=$rate"
      --config "voltage_threshold=$threshold-$threshold"
      --channels "$channels"
      --samples "$samples"
      --output-file "$out")
[ -n "$trigger" ] && args+=(--triggers "$trigger")

sigrok-cli "${args[@]}"

# A channel that never changes is what a probe on the wrong pin looks like,
# and it is worth saying so here rather than three steps later in a decoder.
#
# Per channel, and by the payload only: the bits formatter writes rows as
# "D0:1111 1111", so the channel's own name carries digits that are not
# samples -- counting those makes an all-high D0 look like two levels.
# Each channel keeps two flags, "a 0 was seen" and "a 1 was seen", so the pass
# is linear in the samples: a 4 M-sample channel takes seconds, not minutes.
# A capture with no samples (a missing FPGA bitstream) and a read-back that
# fails are reported as warnings; the path is printed in both cases.
err=$(mktemp)
trap 'rm -f "$err"' EXIT
if ! flat=$(sigrok-cli --input-file "$out" --output-format bits:width=1 2>"$err" |
       awk -F: 'NF > 1 {
           name = $1; gsub(/[ \t]/, "", name);
           bits = $2; gsub(/[^01]/, "", bits);
           if (bits == "") next;
           seen[name] = 1;
           if (index(bits, "0")) zero[name] = 1;
           if (index(bits, "1")) one[name] = 1;
       }
       END {
           for (n in seen) {
               count++;
               if (!(n in zero) || !(n in one))
                   printf "%s ", n;
           }
           if (!count) printf "@empty";
       }'); then
    echo "warning: read-back of $out failed: $(head -n 3 "$err" | tr '\n' ' ')" >&2
    flat=
elif [ "$flat" = "@empty" ]; then
    echo "warning: no samples in the capture -- check the FPGA bitstream (selftest.sh)" >&2
elif [ -n "$flat" ]; then
    echo "warning: never changed: ${flat% } -- probe, threshold or ground" >&2
fi

echo "$out"
