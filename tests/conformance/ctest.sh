#!/usr/bin/env bash
set -eu
seed="${1:?usage: ctest.sh <seed djinnc> [runner flags]}"
shift
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/build/conformance"
mkdir -p "$out/cases"
cd "$root"

bash tools/build_runtime.sh --out "$out/runtime"
runtime_lib="$out/runtime/djinn_runtime.lib"
[ -f "$runtime_lib" ] || runtime_lib="$out/runtime/libdjinn_runtime.a"

flags=(--runtime-dir runtime --runtime-lib "$runtime_lib")
if [ -n "${DJINN_USE_LLD:-}" ]; then
    flags+=(--lld)
fi
"$seed" djinnc -o "$out/djinnc_seeded" "${flags[@]}"
"$out/djinnc_seeded" djinnc -o "$out/djinnc" "${flags[@]}"
bash tests/conformance/build.sh "$out/djinnc" "$runtime_lib" "$out/runner"

"$out/runner" --compiler "$out/djinnc" --runtime-lib "$runtime_lib" --runtime-dir "$root/runtime" \
    --out "$out/cases" --report "$out/report.json" ${DJINN_USE_LLD:+--lld} "$@"
