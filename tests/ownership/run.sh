#!/bin/sh
# Usage: tests/ownership/run.sh [SOURCE_ROOT]
# Builds the ownership harness against SOURCE_ROOT (default: this checkout)
# with ASan/UBSan and runs it. The harness itself always comes from this
# directory, so the same harness can be run against a baseline tree.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "${1:-$HERE/../..}" && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g -O1"
API="$SRC/4D Plugin API"
CC=${CC:-clang}
CXX=${CXX:-clang++}
$CC  $SAN -w -Wno-multichar -Wno-int-conversion -I"$API" -c "$API/4DPluginAPI.c" -o "$OUT/api.o"
$CC  $SAN -w -c "$SRC/support/stb_image.c" -o "$OUT/stb.o"
$CXX $SAN -w -std=c++17 -x objective-c++ -I"$HERE/stub" -I"$API" -I"$API/Classes" -I"$SRC/support" -I"$SRC" -c "$SRC/4DPlugin-zxing.cpp" -o "$OUT/plugin.o"
$CXX $SAN -w -std=c++17 -x objective-c++ -I"$HERE/stub" -I"$API" -I"$API/Classes" -I"$SRC/support" -I"$SRC" -c "$SRC/support/4DPlugin-JSON.cpp" -o "$OUT/json.o"
$CXX $SAN -w -std=c++17 -x objective-c++ -I"$HERE/stub" -I"$API" -I"$API/Classes" -I"$SRC/support" -I"$SRC" -c "$HERE/harness.cpp" -o "$OUT/harness.o"
$CXX $SAN "$OUT"/*.o -framework CoreFoundation -framework CoreGraphics -o "$OUT/harness"
ASAN_OPTIONS=abort_on_error=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 "$OUT/harness"
