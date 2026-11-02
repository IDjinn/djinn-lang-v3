#!/usr/bin/env bash
set -eu
stage1="${1:?usage: fixpoint.sh <stage1 djinnc> <runtime dir>}"
runtime="${2:?usage: fixpoint.sh <stage1 djinnc> <runtime dir>}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$(mktemp -d)"

"$stage1" "$root/djinnc" -o "$out/stage2" --runtime-dir "$runtime" --keep-ir
"$out/stage2" "$root/djinnc" --no-link -o "$out/stage3"

if cmp -s "$out/stage2.ll" "$out/stage3.ll"; then
    echo "PASS fixpoint"
else
    echo "FAIL fixpoint (stage2 and stage3 IR differ)"
    exit 1
fi
