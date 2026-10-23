#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Install the Ubuntu packages the RP2350 firmware builds need, keeping the
# downloaded .deb files in a directory a CI cache can save.
#
#   tools/install_arm_toolchain.sh cache-keys
#   tools/install_arm_toolchain.sh install ARCHIVE_DIR
#
# cache-keys prints two lines for $GITHUB_OUTPUT:
#   key=      exact cache key: Ubuntu release, CPU architecture, runner image
#             version and a hash of the package list
#   restore=  prefix of the key without the image version and the hash, so a
#             changed image or package list restores the previous .deb files
#             and downloads only the ones that differ
#
# install writes the .deb files to ARCHIVE_DIR (apt's package cache for this
# run), downloads them in up to 4 attempts of 300 s each, then installs from
# that directory.  A resumed attempt continues the partial files, and apt
# gives up on a connection that delivers no data for 30 s.  A mirror that
# serves at 50 kB/s keeps delivering data, so the attempt limit is what ends
# the step, at about 20 minutes.  The directory is handed back to the calling
# user so the cache action can read it.  Needs sudo and Ubuntu's apt.
set -euo pipefail

packages=(cmake ninja-build gcc-arm-none-eabi libnewlib-arm-none-eabi
          libstdc++-arm-none-eabi-newlib libusb-1.0-0-dev)

usage() {
    echo "usage: $0 cache-keys | install ARCHIVE_DIR" >&2
    exit 2
}

[ "$#" -ge 1 ] || usage

case "$1" in
cache-keys)
    [ "$#" -eq 1 ] || usage
    # shellcheck disable=SC1091
    release=$(. /etc/os-release && echo "${VERSION_ID}")
    prefix="apt-arm-ubuntu${release}-$(uname -m)-"
    hash=$(printf '%s\n' "${packages[@]}" | sha256sum | cut -c1-12)
    echo "key=${prefix}${ImageVersion:-unknown}-${hash}"
    echo "restore=${prefix}"
    ;;
install)
    [ "$#" -eq 2 ] || usage
    archives=$2
    apt_opts=(-o "Dir::Cache::archives=${archives}"
              -o APT::Keep-Downloaded-Packages=true
              -o Acquire::http::Timeout=30
              -o Acquire::https::Timeout=30
              -o Acquire::Retries=5)

    # Third-party lists this build never installs from; one serving a stale
    # index fails the whole step.  See the note in the host job.
    sudo rm -fv /etc/apt/sources.list.d/*google* \
                /etc/apt/sources.list.d/*microsoft* || true

    mkdir -p "${archives}/partial"

    attempt=1
    until sudo timeout 300 apt-get update "${apt_opts[@]}"; do
        [ "${attempt}" -lt 3 ] || { echo "apt-get update failed" >&2; exit 1; }
        attempt=$((attempt + 1))
    done

    attempt=1
    until sudo timeout 300 apt-get install -y --no-install-recommends \
            --download-only "${apt_opts[@]}" "${packages[@]}"; do
        if [ "${attempt}" -ge 4 ]; then
            echo "download incomplete after ${attempt} attempts" >&2
            exit 1
        fi
        echo "download attempt ${attempt} did not finish, resuming" >&2
        attempt=$((attempt + 1))
    done

    sudo apt-get install -y --no-install-recommends "${apt_opts[@]}" \
        "${packages[@]}"
    sudo chown -R "$(id -u):$(id -g)" "${archives}"
    ;;
*)
    usage
    ;;
esac
