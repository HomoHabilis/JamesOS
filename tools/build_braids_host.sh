#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the MIT Mutable Instruments BRAIDS implementation for native host tests.
set -eu
cd "$(dirname "$0")/.."

CXX="${CXX:-c++}"
OUT=build/host/braids
mkdir -p "$OUT"
rm -f "$OUT"/*.o
for src in \
    firmware/braids/braids_c_api.cc \
    firmware/braids/upstream/braids/analog_oscillator.cc \
    firmware/braids/upstream/braids/digital_oscillator.cc \
    firmware/braids/upstream/braids/macro_oscillator.cc \
    firmware/braids/upstream/braids/resources.cc \
    firmware/braids/braids_perc_c_api.cc; do
    obj="$OUT/braids_$(basename "$src" .cc).o"
    "$CXX" ${BRAIDS_CXXFLAGS:-} -std=c++14 -O2 -fwrapv -fno-exceptions -fno-rtti \
        -fno-threadsafe-statics -Ifirmware/braids/upstream -Ifirmware/braids \
        -c "$src" -o "$obj"
done

# Desktop-only Plaits reference bank. Dinghy target builds remain separately
# gated until the AC79 r3 FPU target flags are verified by the vendor toolchain.
PLOUT=build/host/plaits_reference
mkdir -p "$PLOUT"
rm -f "$PLOUT"/*.o
for src in \
    firmware/plaits/plaits_c_api.cc \
    firmware/plaits/plaits_kit_c_api.cc \
    $(find firmware/plaits/upstream/plaits firmware/plaits/upstream/stmlib -name '*.cc' -type f | sort); do
    rel=${src#firmware/plaits/}
    obj="$PLOUT/plaits_$(printf '%s' "$rel" | tr '/' '_').o"
    "$CXX" ${PLAITS_CXXFLAGS:-} -std=c++14 -O2 -DTEST -ffp-contract=off \
        -fno-exceptions -fno-rtti -fno-threadsafe-statics \
        -Dstmlib=plaits_stmlib \
        -Ifirmware/plaits -Ifirmware/src -Ifirmware/plaits/upstream -Ifirmware/braids/upstream \
        -c "$src" -o "$obj"
done
