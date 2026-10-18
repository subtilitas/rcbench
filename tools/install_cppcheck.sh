#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build cppcheck from the official source archive and install it.
#
#   tools/install_cppcheck.sh VERSION SHA256 PREFIX
#
# Downloads https://github.com/danmar/cppcheck/archive/refs/tags/VERSION.tar.gz,
# stops when its SHA-256 (Secure Hash Algorithm, 256 bit) differs from SHA256,
# builds the cppcheck binary in Release with CMake and Ninja (no tests, no
# GUI) and installs it under PREFIX: PREFIX/bin/cppcheck and the cfg, addons
# and platforms files under PREFIX/share/Cppcheck.  The binary reads those
# files from the path it was built with, so PREFIX is fixed at build time.
# Needs cmake, ninja, a C++ compiler, curl and python3.
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

curl --fail --silent --show-error --location --retry 3 \
     --output "$work/cppcheck.tar.gz" \
     "https://github.com/danmar/cppcheck/archive/refs/tags/${version}.tar.gz"
echo "${sha256}  $work/cppcheck.tar.gz" | sha256sum --check --status || {
    echo "cppcheck ${version}: SHA-256 mismatch, got" \
         "$(sha256sum "$work/cppcheck.tar.gz" | cut -d' ' -f1)" >&2
    exit 1
}

mkdir "$work/src"
tar -xzf "$work/cppcheck.tar.gz" -C "$work/src" --strip-components=1
cmake -S "$work/src" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_TESTS=OFF -DBUILD_GUI=OFF
cmake --build "$work/build" --target cppcheck
cmake --install "$work/build"
"$prefix/bin/cppcheck" --version
