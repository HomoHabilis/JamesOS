#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Compare the compact Plaits Voice with the unchanged pinned upstream Voice.
set -eu
cd "$(dirname "$0")/.."

CXX="${CXX:-c++}"
OUT=build/host/plaits_stock
mkdir -p "$OUT"
rm -f "$OUT"/*.o

# The stock namespace needs ordinary namespace-mangled resource symbols. The
# target's packed-resource ABI intentionally aliases several symbols to the
# compact `plaits` namespace and depends on pre-CPU1 initialization, so disable
# that target-only packaging in the separately namespaced oracle build.
STOCK_RESOURCE_FLAGS="-DFM1_PLAITS_PACK_WAVES=0 -DFM1_PLAITS_PACK_SINE=0 \
-DFM1_PLAITS_PACK_SYX=0 -DFM1_PLAITS_SYX_DECODE_TO_BSS=0 \
-DFM1_PLAITS_WAVES_STACK_SCRATCH=0 -DFM1_BRAIDS_PACK_WT_WAVES=0"

# Build the unmodified upstream engine/model sources in a separate namespace;
# the Plaits stmlib subset is shared with the compact reference build.
for src in $(find firmware/plaits/upstream/plaits -name '*.cc' -type f \
    ! -path '*/dsp/voice.cc' | sort); do
    rel=${src#firmware/plaits/upstream/}
    obj="$OUT/plaits_stock_$(printf '%s' "$rel" | tr '/' '_').o"
    "$CXX" ${PLAITS_CXXFLAGS:-} $STOCK_RESOURCE_FLAGS -std=c++14 -O2 -DTEST -ffp-contract=off \
        -fno-exceptions -fno-rtti -fno-threadsafe-statics \
        -Dplaits=plaits_stock -Dstmlib=plaits_stmlib \
        -Itests/plaits_reference -Ifirmware/plaits -Ifirmware/plaits/upstream \
        -Ifirmware/braids/upstream -c "$src" -o "$obj"
done
"$CXX" ${PLAITS_CXXFLAGS:-} $STOCK_RESOURCE_FLAGS -std=c++14 -O2 -DTEST -ffp-contract=off \
    -fno-exceptions -fno-rtti -fno-threadsafe-statics \
    -DPLAITS_TEST_STOCK -Dplaits=plaits_stock -Dstmlib=plaits_stmlib \
    -Itests/plaits_reference -Ifirmware/plaits -Ifirmware/plaits/upstream \
    -Ifirmware/braids/upstream -c tests/plaits_reference/plaits/dsp/voice.cc \
    -o "$OUT/plaits_stock_voice.o"

"$CXX" ${PLAITS_CXXFLAGS:-} -std=c++14 -O2 -DTEST -ffp-contract=off \
    -fno-exceptions -fno-rtti -fno-threadsafe-statics \
    -Dstmlib=plaits_stmlib -Ifirmware/plaits -Ifirmware/plaits/upstream \
    -Ifirmware/braids/upstream -c tests/plaits_voice_oracle.cc \
    -o "$OUT/plaits_compact_oracle.o"
"$CXX" ${PLAITS_CXXFLAGS:-} $STOCK_RESOURCE_FLAGS -std=c++14 -O2 -DTEST -ffp-contract=off \
    -fno-exceptions -fno-rtti -fno-threadsafe-statics \
    -DPLAITS_TEST_STOCK -Dplaits=plaits_stock -Dstmlib=plaits_stmlib \
    -Itests/plaits_reference -Ifirmware/plaits -Ifirmware/plaits/upstream \
    -Ifirmware/braids/upstream -c tests/plaits_voice_oracle.cc \
    -o "$OUT/plaits_stock_oracle.o"

STOCK_OBJS=$(find "$OUT" -name '*.o' -type f ! -name '*oracle.o' | sort)
"$CXX" -O2 -ffp-contract=off -Ifirmware/plaits -Ifirmware/plaits/upstream \
    -o build/host/plaits_engine_test tests/plaits_engine_test.cc \
    "$OUT/plaits_compact_oracle.o" "$OUT/plaits_stock_oracle.o" \
    build/host/plaits_reference/*.o $STOCK_OBJS \
    build/host/braids/braids_resources.o
