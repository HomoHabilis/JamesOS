#!/bin/sh
set -eu

testroot=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
outdir=$(mktemp -d "${TMPDIR:-/tmp}/fault22-stack-abi.XXXXXX")
trap 'rm -rf "$outdir"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -Wall -Wextra -Werror \
    "$testroot/tools/test_fault22_stack_abi.c" -o "$outdir/normal"
"$outdir/normal"

${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer "$testroot/tools/test_fault22_stack_abi.c" \
    -o "$outdir/sanitized"
"$outdir/sanitized"

printf '%s\n' 'C0 stack-window ABI host tests passed (normal + ASan/UBSan).'
