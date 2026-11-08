#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Compile Plaits against its freestanding target headers and compare it with
# the unchanged upstream Voice oracle. All objects stay in a private directory.
set -eu
cd "$(dirname "$0")/.."

CXX="${CXX:-c++}"
CC="${CC:-cc}"
OUT=build/host/plaits_target_headers
mkdir -p "$OUT"
rm -f "$OUT"/*.o "$OUT"/plaits_engine_test "$OUT"/memmove_test

CXXFLAGS="${PLAITS_CXXFLAGS:-} -std=c++14 -O2 -DTEST -ffp-contract=off -fno-exceptions -fno-rtti -fno-threadsafe-statics"
TARGET_INCLUDES="-Ifirmware/target/include -Ifirmware/plaits -Ifirmware/plaits/upstream -Ifirmware/braids/upstream"
STOCK_INCLUDES="-Ifirmware/target/include -Itests/plaits_reference -Ifirmware/plaits -Ifirmware/plaits/upstream -Ifirmware/braids/upstream"

for src in $(find firmware/plaits/upstream/plaits -name '*.cc' -type f ! -path '*/dsp/voice.cc' | sort); do
    rel=${src#firmware/plaits/upstream/}
    obj="$OUT/plaits_compact_$(printf '%s' "$rel" | tr '/' '_').o"
    # shellcheck disable=SC2086
    "$CXX" $CXXFLAGS $TARGET_INCLUDES -Dstmlib=plaits_stmlib -c "$src" -o "$obj"
done

for src in $(find firmware/plaits/upstream/plaits -name '*.cc' -type f ! -path '*/dsp/voice.cc' | sort); do
    rel=${src#firmware/plaits/upstream/}
    obj="$OUT/plaits_stock_$(printf '%s' "$rel" | tr '/' '_').o"
    # shellcheck disable=SC2086
    "$CXX" $CXXFLAGS $STOCK_INCLUDES -Dplaits=plaits_stock -Dstmlib=plaits_stmlib \
        -c "$src" -o "$obj"
done

for src in $(find firmware/plaits/upstream/stmlib -name '*.cc' -type f | sort); do
    rel=${src#firmware/plaits/upstream/}
    obj="$OUT/plaits_shared_$(printf '%s' "$rel" | tr '/' '_').o"
    # shellcheck disable=SC2086
    "$CXX" $CXXFLAGS $TARGET_INCLUDES -Dstmlib=plaits_stmlib -c "$src" -o "$obj"
done

"$CXX" $CXXFLAGS $TARGET_INCLUDES -Dstmlib=plaits_stmlib \
    -c firmware/plaits/plaits_c_api.cc -o "$OUT/plaits_c_api.o"
"$CXX" $CXXFLAGS $TARGET_INCLUDES -Dstmlib=plaits_stmlib \
    -c firmware/plaits/plaits_kit_c_api.cc -o "$OUT/plaits_kit_c_api.o"
"$CXX" $CXXFLAGS $TARGET_INCLUDES -Dstmlib=plaits_stmlib \
    -c firmware/plaits/upstream/plaits/dsp/voice.cc -o "$OUT/plaits_compact_voice.o"
"$CXX" $CXXFLAGS $TARGET_INCLUDES -Dstmlib=plaits_stmlib \
    -c tests/plaits_voice_oracle.cc -o "$OUT/plaits_compact_oracle.o"
"$CXX" $CXXFLAGS $STOCK_INCLUDES -DPLAITS_TEST_STOCK \
    -Dplaits=plaits_stock -Dstmlib=plaits_stmlib \
    -c tests/plaits_voice_oracle.cc -o "$OUT/plaits_stock_oracle.o"
"$CXX" $CXXFLAGS $STOCK_INCLUDES -include cstdio -Dplaits=plaits_stock \
    -Dstmlib=plaits_stmlib \
    -c tests/plaits_reference/plaits/dsp/voice.cc -o "$OUT/plaits_stock_voice.o"

STOCK_OBJS=$(find "$OUT" -name 'plaits_stock_*.o' -type f \
    ! -name 'plaits_stock_oracle.o' ! -name 'plaits_stock_voice.o' | sort)
COMPACT_OBJS=$(find "$OUT" -name 'plaits_compact_*.o' -type f \
    ! -name 'plaits_compact_oracle.o' ! -name 'plaits_compact_voice.o' | sort)
SHARED_OBJS=$(find "$OUT" -name 'plaits_shared_*.o' -type f | sort)
# shellcheck disable=SC2086
"$CXX" -O2 -ffp-contract=off $TARGET_INCLUDES -o "$OUT/plaits_engine_test" \
    tests/plaits_engine_test.cc "$OUT/plaits_compact_oracle.o" \
    "$OUT/plaits_stock_oracle.o" "$OUT/plaits_stock_voice.o" \
    "$OUT/plaits_c_api.o" "$OUT/plaits_kit_c_api.o" \
    "$OUT/plaits_compact_voice.o" $SHARED_OBJS $COMPACT_OBJS $STOCK_OBJS
"$OUT/plaits_engine_test"

"$CC" -std=c11 -O2 -Wall -Wextra -o "$OUT/memmove_test" \
    tests/target_memmove_test.c
"$OUT/memmove_test"
