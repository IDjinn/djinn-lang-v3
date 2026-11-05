#!/usr/bin/env bash
set -u
compiler="${1:?usage: run.sh <djinnc executable> <runtime dir> [runtime lib]}"
runtime="${2:?usage: run.sh <djinnc executable> <runtime dir> [runtime lib]}"
runtime_lib="${3:-}"
link_flags=()
if [ -n "$runtime_lib" ]; then
    link_flags+=(--runtime-lib "$runtime_lib")
    if [ -n "${DJINN_USE_LLD:-}" ]; then
        link_flags+=(--lld)
    fi
fi
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mktemp -d)"
failed=0

if ! "$compiler" --selftest; then
    echo "FAIL selftest"
    failed=1
else
    echo "PASS selftest"
fi

for source in "$here"/samples/*.djinn; do
    name="$(basename "$source" .djinn)"
    expected="$(cat "$here/samples/$name.exit")"
    if ! "$compiler" "$source" -o "$out/$name" --runtime-dir "$runtime" ${link_flags[@]+"${link_flags[@]}"} >"$out/$name.log" 2>&1; then
        echo "FAIL $name (compile)"
        cat "$out/$name.log"
        failed=1
        continue
    fi
    "$out/$name" >"$out/$name.stdout"
    actual=$?
    if [ "$actual" != "$expected" ]; then
        echo "FAIL $name (expected exit $expected, got $actual)"
        failed=1
    elif [ -f "$here/samples/$name.stdout" ] && ! diff <(tr -d '\r' <"$out/$name.stdout") "$here/samples/$name.stdout" >/dev/null; then
        echo "FAIL $name (stdout differs)"
        diff <(tr -d '\r' <"$out/$name.stdout") "$here/samples/$name.stdout" | head -10
        failed=1
    else
        echo "PASS $name"
    fi
done

std_dir="${DJINN_STD_DIR:-$here/../../std}"
for source in "$here"/std/*.djinn; do
    name="$(basename "$source" .djinn)"
    expected="$(cat "$here/std/$name.exit")"
    if ! "$compiler" "$source" --std --std-dir "$std_dir" -o "$out/std_$name" --runtime-dir "$runtime" ${link_flags[@]+"${link_flags[@]}"} >"$out/std_$name.log" 2>&1; then
        echo "FAIL std/$name (compile)"
        cat "$out/std_$name.log"
        failed=1
        continue
    fi
    "$out/std_$name" >"$out/std_$name.stdout"
    actual=$?
    if [ "$actual" != "$expected" ]; then
        echo "FAIL std/$name (expected exit $expected, got $actual)"
        failed=1
    elif [ -f "$here/std/$name.stdout" ] && ! diff <(tr -d '\r' <"$out/std_$name.stdout") "$here/std/$name.stdout" >/dev/null; then
        echo "FAIL std/$name (stdout differs)"
        failed=1
    else
        echo "PASS std/$name"
    fi
done

for source in "$here"/errors/*.djinn; do
    name="$(basename "$source" .djinn)"
    if "$compiler" "$source" --no-link -o "$out/$name" >"$out/$name.log" 2>&1; then
        echo "FAIL $name (expected a compile error)"
        failed=1
    elif ! grep -q "error:" "$out/$name.log"; then
        echo "FAIL $name (no diagnostic)"
        failed=1
    else
        echo "PASS $name"
    fi
done

exit $failed
