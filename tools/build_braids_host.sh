#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the MIT Mutable Instruments BRAIDS implementation for native host tests.
# The 48 translation units are compiled in parallel, at most BR_DSP_JOBS at a time (default 4).
# Objects, names and flags are the same as a serial build; the script fails if any compile fails.
set -eu
cd "$(dirname "$0")/.."

JOBS="${BR_DSP_JOBS:-4}"
case "$JOBS" in
    ''|*[!0-9]*|0) echo "build_braids_host.sh: BR_DSP_JOBS must be a positive integer (got '$JOBS')" >&2; exit 2 ;;
esac

OUT=build/host/braids
mkdir -p "$OUT"
rm -f "$OUT"/*.o
PLOUT=build/host/plaits_reference
mkdir -p "$PLOUT"
rm -f "$PLOUT"/*.o

# One line per compile: kind source object. Braids first, then the desktop-only Plaits reference bank
# (Dinghy target builds remain separately gated until the AC79 r3 FPU target flags are verified by the
# vendor toolchain).
{
    for src in \
        firmware/braids/braids_c_api.cc \
        firmware/braids/upstream/braids/analog_oscillator.cc \
        firmware/braids/upstream/braids/digital_oscillator.cc \
        firmware/braids/upstream/braids/macro_oscillator.cc \
        firmware/braids/upstream/braids/resources.cc \
        firmware/braids/braids_perc_c_api.cc; do
        echo "braids $src $OUT/braids_$(basename "$src" .cc).o"
    done
    for src in \
        firmware/plaits/plaits_c_api.cc \
        firmware/plaits/plaits_kit_c_api.cc \
        $(find firmware/plaits/upstream/plaits firmware/plaits/upstream/stmlib -name '*.cc' -type f | sort); do
        rel=${src#firmware/plaits/}
        echo "plaits $src $PLOUT/plaits_$(printf '%s' "$rel" | tr '/' '_').o"
    done
} | xargs -L 1 -P "$JOBS" sh -c '
    CXX="${CXX:-c++}"
    kind=$1; src=$2; obj=$3
    if [ "$kind" = braids ]; then
        exec "$CXX" ${BRAIDS_CXXFLAGS:-} -std=c++14 -O2 -fwrapv -fno-exceptions -fno-rtti \
            -fno-threadsafe-statics -Ifirmware/braids/upstream -Ifirmware/braids \
            -c "$src" -o "$obj"
    else
        exec "$CXX" ${PLAITS_CXXFLAGS:-} -std=c++14 -O2 -DTEST -ffp-contract=off \
            -fno-exceptions -fno-rtti -fno-threadsafe-statics \
            -Dstmlib=plaits_stmlib \
            -Ifirmware/plaits -Ifirmware/src -Ifirmware/plaits/upstream -Ifirmware/braids/upstream \
            -c "$src" -o "$obj"
    fi' _
