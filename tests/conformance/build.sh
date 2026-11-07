#!/usr/bin/env bash
set -eu
compiler="${1:?usage: build.sh <djinnc> <runtime lib> <output> [runtime dir]}"
runtime_lib="${2:?usage: build.sh <djinnc> <runtime lib> <output> [runtime dir]}"
output="${3:?usage: build.sh <djinnc> <runtime lib> <output> [runtime dir]}"
runtime="${4:-runtime}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
libs="$(dirname "$output")/libs"
mkdir -p "$libs"
flags=(--lib-dir "$libs" --std-dir "$root/std" --runtime-dir "$runtime" --runtime-lib "$runtime_lib")
if [ -n "${DJINN_USE_LLD:-}" ]; then
    flags+=(--lld)
fi
"$compiler" "$root/libs/testing" -o "$libs/testing.djlib" "${flags[@]}"
"$compiler" "$root/djinnc/testing" -o "$libs/djinnc_testing.djlib" "${flags[@]}"
"$compiler" "$root/tests/conformance" -o "$output" "${flags[@]}"
