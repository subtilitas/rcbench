#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Install the doxygen binary of an official Linux x86-64 release.
#
#   tools/install_doxygen.sh VERSION SHA256 PREFIX
#
# Downloads doxygen-VERSION.linux.bin.tar.gz (115 MB for 1.18.0) from the
# release Release_<VERSION with _ for .> on github.com/doxygen/doxygen,
# stops when its SHA-256 (Secure Hash Algorithm, 256 bit) differs from
# SHA256, and installs the doxygen binary alone as PREFIX/bin/doxygen.  The
# binary links libc, libstdc++, libm, zlib and libtinfo only.  It prints the
# installed version and stops when it is not VERSION.
# Needs curl, tar and sha256sum.
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "usage: $0 VERSION SHA256 PREFIX" >&2
    exit 2
fi
version=$1
sha256=$2
prefix=$3

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

archive="doxygen-${version}.linux.bin.tar.gz"
curl --fail --silent --show-error --location --retry 3 \
     --output "$work/$archive" \
     "https://github.com/doxygen/doxygen/releases/download/Release_${version//./_}/$archive"
echo "${sha256}  $work/$archive" | sha256sum --check --status || {
    echo "doxygen ${version}: SHA-256 mismatch, got" \
         "$(sha256sum "$work/$archive" | cut -d' ' -f1)" >&2
    exit 1
}

mkdir -p "$prefix/bin"
tar -xzf "$work/$archive" -C "$work" "doxygen-${version}/bin/doxygen"
install -m 755 "$work/doxygen-${version}/bin/doxygen" "$prefix/bin/doxygen"
installed=$("$prefix/bin/doxygen" --version | cut -d' ' -f1)
echo "doxygen $installed"
[ "$installed" = "$version" ]
