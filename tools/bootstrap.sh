#!/usr/bin/env bash
set -eu

usage() {
    echo "usage: bootstrap.sh --seed <djinnc executable> --runtime-dir <dir> [--runtime-lib <lib>] [--lld] [--out <dir>]"
    echo "                    [--bench-ref <file>] [--update-ref]"
    exit 2
}

root="$(cd "$(dirname "$0")/.." && pwd)"
seed=""
runtime=""
runtime_lib=""
use_lld=0
out="$root/build/bootstrap"
bench_ref="$root/tools/bench.reference.json"
update_ref=0

while [ $# -gt 0 ]; do
    case "$1" in
        --seed) seed="$2"; shift 2 ;;
        --runtime-dir) runtime="$2"; shift 2 ;;
        --runtime-lib) runtime_lib="$2"; shift 2 ;;
        --lld) use_lld=1; shift ;;
        --out) out="$2"; shift 2 ;;
        --bench-ref) bench_ref="$2"; shift 2 ;;
        --update-ref) update_ref=1; shift ;;
        *) usage ;;
    esac
done
[ -n "$seed" ] && [ -n "$runtime" ] || usage
[ -f "$seed" ] || { echo "seed not found: $seed"; exit 2; }
seed="$(cd "$(dirname "$seed")" && pwd)/$(basename "$seed")"

ext=""
case "$seed" in *.exe) ext=".exe" ;; esac

opt_flags=(-O2)

link_flags=()
if [ -n "$runtime_lib" ]; then
    link_flags+=(--runtime-lib "$runtime_lib")
    [ "$use_lld" = 1 ] && link_flags+=(--lld)
fi

now_ms() { echo $(( $(date +%s%N) / 1000000 )); }

rm -rf "$out"
mkdir -p "$out/stage1" "$out/stage2" "$out/stage3" "$out/samples"
cd "$root"

echo "== seed -> stage1"
"$seed" djinnc -o "$out/stage1/djinnc$ext" --runtime-dir "$runtime" "${opt_flags[@]}" ${link_flags[@]+"${link_flags[@]}"} >"$out/stage1.log" 2>&1 \
    || { echo "FAIL stage1 not produced (see $out/stage1.log)"; exit 1; }

echo "== stage1 -> stage2"
"$out/stage1/djinnc$ext" djinnc -o "$out/stage2/djinnc$ext" --runtime-dir "$runtime" "${opt_flags[@]}" ${link_flags[@]+"${link_flags[@]}"} --keep-ir

echo "== stage2 -> stage3"
"$out/stage2/djinnc$ext" djinnc -o "$out/stage3/djinnc$ext" --runtime-dir "$runtime" "${opt_flags[@]}" ${link_flags[@]+"${link_flags[@]}"} --keep-ir

echo "== fixpoint"
if cmp -s "$out/stage2/djinnc$ext.ll" "$out/stage3/djinnc$ext.ll"; then
    echo "PASS fixpoint"
else
    echo "FAIL fixpoint (stage2 and stage3 IR differ)"
    diff "$out/stage2/djinnc$ext.ll" "$out/stage3/djinnc$ext.ll" | head -20 || true
    exit 1
fi

echo "== samples on stage1, stage2 and stage3"
absolute_out="$(cd "$out" && pwd)"
runner_lib="$runtime_lib"
if [ -z "$runner_lib" ]; then
    bash "$root/tools/build_runtime.sh" --out "$out/runtime" >"$out/runtime.log" 2>&1
    runner_lib="$absolute_out/runtime/djinn_runtime.lib"
fi
runner_flags=(--runtime-lib "$runner_lib" --runtime-dir "$runtime" --require Link --filter SamplesTests --out "$absolute_out/samples")
[ "$use_lld" = 1 ] && runner_flags+=(--lld)
DJINN_USE_LLD="${use_lld/#0/}" bash "$root/tests/conformance/build.sh" "$absolute_out/stage2/djinnc$ext" "$runner_lib" "$out/runner$ext" "$runtime"
for stage in stage1 stage2 stage3; do
    "$out/runner$ext" --compiler "$absolute_out/$stage/djinnc$ext" "${runner_flags[@]}" >"$out/samples.$stage.log" || { grep FAIL "$out/samples.$stage.log"; exit 1; }
    echo "PASS samples on $stage"
done
"$out/runner$ext" --compiler "$absolute_out/stage2/djinnc$ext" "${runner_flags[@]}" --link-debug >"$out/samples.debug.log" || { grep FAIL "$out/samples.debug.log"; exit 1; }
echo "PASS samples with -g"

echo "== benchmark (stage2 compiling djinnc, best of 3)"
best=0
for run in 1 2 3; do
    start="$(now_ms)"
    "$out/stage2/djinnc$ext" djinnc --no-link --stats -o "$out/bench_run" >"$out/bench.stats" 2>&1
    elapsed=$(( $(now_ms) - start ))
    if [ "$best" = 0 ] || [ "$elapsed" -lt "$best" ]; then best="$elapsed"; fi
done
tokens="$(sed -n 's/^tokens: \([0-9]*\).*/\1/p' "$out/bench.stats")"
nodes="$(sed -n 's/^ast: \([0-9]*\) nodes.*/\1/p' "$out/bench.stats")"
arena="$(sed -n 's/^arena: \([0-9]*\) bytes allocated.*/\1/p' "$out/bench.stats")"
phase_us() {
    local value
    value="$(sed -n "s/^\[PerformanceMonitor [^]]*\] lap $1 ([0-9]*) \([0-9]*\)\.\([0-9]*\) ms/\1\2/p" "$out/bench.stats" | head -1)"
    [ -n "$value" ] && echo $(( 10#$value ))
}
lex_us="$(phase_us lex)"
parse_us="$(phase_us parse)"
codegen_us="$(phase_us codegen)"
cat >"$out/bench.json" <<JSON
{
  "compile_ms": $best,
  "tokens": ${tokens:-0},
  "nodes": ${nodes:-0},
  "arena_bytes": ${arena:-0},
  "lex_us": ${lex_us:-0},
  "parse_us": ${parse_us:-0},
  "codegen_us": ${codegen_us:-0}
}
JSON
cat "$out/bench.json"

if [ "$update_ref" = 1 ]; then
    cp "$out/bench.json" "$bench_ref"
    echo "reference updated: $bench_ref"
elif [ -f "$bench_ref" ]; then
    for key in compile_ms arena_bytes; do
        old="$(sed -n "s/.*\"$key\": *\([0-9]*\).*/\1/p" "$bench_ref")"
        new="$(sed -n "s/.*\"$key\": *\([0-9]*\).*/\1/p" "$out/bench.json")"
        if [ -n "$old" ] && [ "$old" -gt 0 ] && [ $(( new * 100 )) -gt $(( old * 110 )) ]; then
            echo "FAIL benchmark regression: $key $old -> $new (> 10%)"
            exit 1
        fi
    done
    echo "PASS benchmark (within 10% of reference)"
else
    echo "no benchmark reference at $bench_ref (use --update-ref to create it)"
fi

echo "PASS bootstrap"
