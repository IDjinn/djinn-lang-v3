#!/usr/bin/env bash
set -u
compiler="${1:?usage: run.sh <djinnc executable> <runtime dir>}"
runtime="${2:?usage: run.sh <djinnc executable> <runtime dir>}"
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mktemp -d)"
failed=0

for source in "$here"/samples/*.djinn; do
    name="$(basename "$source" .djinn)"
    expected="$(cat "$here/samples/$name.expected")"
    if ! "$compiler" "$source" -o "$out/$name" --runtime-dir "$runtime" >"$out/$name.log" 2>&1; then
        echo "FAIL $name (compile)"
        cat "$out/$name.log"
        failed=1
        continue
    fi
    "$out/$name" >/dev/null
    actual=$?
    if [ "$actual" != "$expected" ]; then
        echo "FAIL $name (expected $expected, got $actual)"
        failed=1
    else
        echo "PASS $name"
    fi
done

exit $failed
