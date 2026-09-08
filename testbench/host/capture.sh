#!/usr/bin/env bash
#
# One capture, into an artefact that says what it is.
#
#   capture.sh <name> <channels> <samplerate> <samples> [trigger]
#
#   capture.sh dshot300-idle D0 24m 2m
#   capture.sh dshot300-arm  D0,D6 24m 4m D0=r
#
# The sample rate is the first thing to get right: a DShot600 bit is about
# 1.67 us, so 24 MHz gives 40 samples a bit and a rate low enough to hold
# 16 channels.  Reading a 1 us servo step needs no more than 8 MHz; reading a
# bit edge does.
#
# Writes captures/<name>-<timestamp>.sr and prints the path.  Nothing here
# decodes: a capture is evidence and stays as it was taken.
#
# SPDX-License-Identifier: MIT
set -euo pipefail

if [ $# -lt 4 ]; then
    sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi

name=$1
channels=$2
rate=$3
samples=$4
trigger=${5:-}

here=$(cd "$(dirname "$0")/.." && pwd)
out="$here/captures/${name}-$(date -u +%Y%m%dT%H%M%SZ).sr"

args=(--driver kingst-la2016
      --config "samplerate=$rate"
      --channels "$channels"
      --samples "$samples"
      --output-file "$out")
[ -n "$trigger" ] && args+=(--triggers "$trigger")

sigrok-cli "${args[@]}"

# A capture of the right length that is all one level is what a probe on the
# wrong pin looks like, and it is worth saying so here rather than three
# steps later in a decoder.
levels=$(sigrok-cli --input-file "$out" --output-format bits:width=1 2>/dev/null \
         | tr -d ' \n' | tr -d '[:alpha:]:' | fold -w1 | sort -u | tr -d '\n')
if [ "${#levels}" -le 1 ]; then
    echo "warning: every sample is the same level -- probe, threshold or ground" >&2
fi

echo "$out"
