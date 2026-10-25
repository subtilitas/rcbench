#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Install the doxygen binary of an official Linux x86-64 release.
#
#   tools/install_doxygen.sh VERSION SHA256 PREFIX [ARCHIVE_DIR]
#
# Takes doxygen-VERSION.linux.bin.tar.gz (115MB for 1.18.0) from
# ARCHIVE_DIR when it is there, and otherwise downloads it from the release
# Release_<VERSION with _ for .> on github.com/doxygen/doxygen, into
# ARCHIVE_DIR when one is given.  Stops when the archive's SHA-256 (Secure
# Hash Algorithm, 256 bit) differs from SHA256, a cached archive included,
# and installs the doxygen binary alone as PREFIX/bin/doxygen.  The binary
# links libc, libstdc++, libm, zlib and libtinfo only.  Prints the
# installed version and stops when it is not VERSION.
# Needs curl, tar and sha256sum: Linux on x86-64.
set -euo pipefail

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo "usage: $0 VERSION SHA256 PREFIX [ARCHIVE_DIR]" >&2
    exit 2
fi
version=$1
sha256=$2
prefix=$3

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
store=${4:-$work}
mkdir -p "$store"

archive="doxygen-${version}.linux.bin.tar.gz"
release="Release_${version//./_}"
url="https://github.com/doxygen/doxygen/releases/download/$release/$archive"
if [ ! -f "$store/$archive" ]; then
    curl --fail --silent --show-error --location --retry 3 \
         --output "$store/$archive.part" "$url"
    mv "$store/$archive.part" "$store/$archive"
fi
echo "${sha256}  $store/$archive" | sha256sum --check --status || {
    echo "doxygen ${version}: SHA-256 mismatch, got" \
         "$(sha256sum "$store/$archive" | cut -d' ' -f1)" >&2
    rm -f "$store/$archive"
    exit 1
}

mkdir -p "$prefix/bin"
tar -xzf "$store/$archive" -C "$work" "doxygen-${version}/bin/doxygen"
install -m 755 "$work/doxygen-${version}/bin/doxygen" "$prefix/bin/doxygen"
installed=$("$prefix/bin/doxygen" --version | cut -d' ' -f1)
echo "doxygen $installed"
[ "$installed" = "$version" ]
