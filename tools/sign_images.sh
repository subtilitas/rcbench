#!/usr/bin/env bash
# Sign the firmware images of a release, and verify the signatures.
#
# An image is a file named *.bin or *.uf2.  Its signature is <image>.sig
# beside it: ECDSA (elliptic curve digital signature algorithm) P-256 over
# the SHA-256 of the whole file, in the DER (distinguished encoding rules)
# form that `openssl dgst -sha256 -sign` writes, 8 to 72 bytes.
#
#   tools/sign_images.sh sign DIR
#       Sign every image under DIR, at any depth.  The private key is the
#       PEM text in the environment variable SIGNING_KEY; an empty or unset
#       variable fails before anything is signed.  The key exists only in a
#       file created under umask 077, removed on every exit.
#
#   tools/sign_images.sh verify PUBKEY DIR
#       Check every image directly in DIR and every image inside each ZIP
#       file directly in DIR: each has its .sig, and the .sig verifies under
#       the public key in the PEM file PUBKEY.  A .sig without its image
#       fails too.  Prints one line per image checked.
#
# Exit status 0 when every step passed, 1 otherwise, 2 for a usage error.
#
# SPDX-License-Identifier: MIT
set -euo pipefail

usage() {
    echo "usage: $0 sign DIR | verify PUBKEY DIR" >&2
    exit 2
}

# The images under $1, one a line, at any depth.
images_under() {
    find "$1" -type f \( -name '*.bin' -o -name '*.uf2' \) | LC_ALL=C sort
}

sign() {
    local dir=$1
    [ -d "$dir" ] || { echo "::error::no folder $dir" >&2; exit 1; }
    if [ -z "${SIGNING_KEY:-}" ]; then
        echo "::error::SIGNING_KEY is empty: the secret RCBENCH_IMAGE_SIGNING is not set" >&2
        exit 1
    fi
    local key
    key=$(umask 077 && mktemp)
    # shellcheck disable=SC2064 # the path is fixed now
    trap "rm -f '$key'" EXIT
    printf '%s\n' "$SIGNING_KEY" > "$key"
    local n=0 f
    while IFS= read -r f; do
        openssl dgst -sha256 -sign "$key" -out "$f.sig" "$f"
        n=$((n + 1))
    done < <(images_under "$dir")
    [ "$n" -gt 0 ] || { echo "::error::no image under $dir" >&2; exit 1; }
    echo "signed $n images under $dir"
}

# Check one image $2 against $2.sig under the public key $1; $3 names it.
check_one() {
    local pub=$1 img=$2 name=$3
    if [ ! -f "$img.sig" ]; then
        echo "::error::$name has no signature"
        return 1
    fi
    if ! openssl dgst -sha256 -verify "$pub" -signature "$img.sig" "$img" \
            > /dev/null 2>&1; then
        echo "::error::the signature of $name does not verify"
        return 1
    fi
    echo "ok $name"
}

verify() {
    local pub=$1 dir=$2
    [ -f "$pub" ] || { echo "::error::no public key $pub" >&2; exit 1; }
    [ -d "$dir" ] || { echo "::error::no folder $dir" >&2; exit 1; }
    local bad=0 n=0 f z tmp sub
    for f in "$dir"/*.bin "$dir"/*.uf2; do
        [ -e "$f" ] || continue
        check_one "$pub" "$f" "$(basename "$f")" || bad=1
        n=$((n + 1))
    done
    for f in "$dir"/*.sig; do
        [ -e "$f" ] || continue
        [ -f "${f%.sig}" ] || { echo "::error::$(basename "$f") has no image"; bad=1; }
    done
    tmp=$(mktemp -d)
    # shellcheck disable=SC2064 # the path is fixed now
    trap "rm -rf '$tmp'" EXIT
    for z in "$dir"/*.zip; do
        [ -e "$z" ] || continue
        sub="$tmp/$(basename "$z")"
        mkdir -p "$sub"
        unzip -q "$z" -d "$sub"
        while IFS= read -r f; do
            check_one "$pub" "$f" "$(basename "$z"):${f#"$sub"/}" || bad=1
            n=$((n + 1))
        done < <(images_under "$sub")
        while IFS= read -r f; do
            [ -f "${f%.sig}" ] || {
                echo "::error::$(basename "$z"):${f#"$sub"/} has no image"
                bad=1
            }
        done < <(find "$sub" -type f -name '*.sig' | LC_ALL=C sort)
    done
    [ "$n" -gt 0 ] || { echo "::error::no image in $dir" >&2; exit 1; }
    if [ "$bad" -ne 0 ]; then
        echo "::error::an image is unsigned or its signature does not verify"
        exit 1
    fi
    echo "$n images signed by the key in $pub"
}

[ $# -ge 1 ] || usage
case "$1" in
    sign)   [ $# -eq 2 ] || usage; sign "$2" ;;
    verify) [ $# -eq 3 ] || usage; verify "$2" "$3" ;;
    *)      usage ;;
esac
