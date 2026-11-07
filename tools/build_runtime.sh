#!/usr/bin/env bash
set -eu

usage() {
    echo "usage: build_runtime.sh [--out <dir>]"
    echo "builds the release runtime library (djinn_runtime.lib on Windows, libdjinn_runtime.a elsewhere)"
    echo "environment: DJINN_CLANG (default clang), DJINN_AR (default llvm-lib on Windows, llvm-ar elsewhere)"
    exit 2
}

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/build/runtime"

while [ $# -gt 0 ]; do
    case "$1" in
        --out) out="$2"; shift 2 ;;
        *) usage ;;
    esac
done

clang="${DJINN_CLANG:-clang}"
flags=(-c -O2 -I "$root/runtime")
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        windows=1
        flags+=(-std=c17 -fms-runtime-lib=dll)
        archive="$out/djinn_runtime.lib"
        archiver="${DJINN_AR:-llvm-lib}"
        ;;
    *)
        windows=0
        flags+=(-std=c2x -fPIC)
        archive="$out/libdjinn_runtime.a"
        archiver="${DJINN_AR:-llvm-ar}"
        ;;
esac

mkdir -p "$out"
rm -f "$archive"
for name in runtime_all logger; do
    "$clang" "${flags[@]}" "$root/runtime/$name.c" -o "$out/$name.o"
done
"$clang" "${flags[@]}" "$root/runtime/third_party/yyjson/yyjson.c" -o "$out/yyjson.o"
"$clang" "${flags[@]}" "$root/runtime/json/djinn_json.c" -o "$out/djinn_json.o"

if [ "$windows" = 1 ]; then
    "$archiver" "-out:$archive" "$out/runtime_all.o" "$out/logger.o" "$out/yyjson.o" "$out/djinn_json.o"
else
    "$archiver" rcs "$archive" "$out/runtime_all.o" "$out/logger.o" "$out/yyjson.o" "$out/djinn_json.o"
fi
echo "$archive"
