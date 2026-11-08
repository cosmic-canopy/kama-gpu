#!/usr/bin/env bash
# tools/check-bindings.sh — the WebGPU bindings are what the headers make, and their numbers are the headers'.
#
#   tools/check-bindings.sh            # regenerate into out/, compare with src/native/webgpu.gen.kama, check constants
#   tools/check-bindings.sh --write    # regenerate and replace src/native/webgpu.gen.kama (after a header moves)
#
# Run it from msys2 UCRT64 on Windows (kama's docs/platforms/windows.md), or any shell elsewhere. It needs:
#   - kama: `kama` on PATH, or $KAMA, or ../kama/out/<platform>/kama.exe beside this repository;
#   - wgpu-native's SDK at $KAMA_WGPU_DIR (default third_party/wgpu/<platform>, fetched by kama's
#     tools/fetch-webgpu.sh with WGPU_NATIVE_VERSION pinned to the one the banner names);
#   - emscripten's emdawnwebgpu port under $EMSDK (default: the emsdk `emcc` runs from, else ~/emsdk): building
#     anything with "webgpu": true for the web once fetches it. The web leg's constant check also needs `emcc` on PATH, and is skipped, said so, without.
#
# The constant check compiles one `_Static_assert` per constant (wgpugen's second output) against each leg's real
# header and throws the object away; if it compiles, the numbers agree. It runs as C, and as C++ only when C refuses,
# since an older clang does not fold a C `static const` (friendly-fire-department's tools/check-wgpu-constants, FR-29).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) PLAT="${MSYSTEM:-$(uname -s)}-$(uname -m)" ;;
    *) PLAT="$(uname -s)-$(uname -m)" ;;
esac

if [ -n "${KAMA:-}" ]; then :
elif command -v kama >/dev/null 2>&1; then KAMA=kama
elif [ -x "$ROOT/../kama/out/$PLAT/kama.exe" ]; then KAMA="$ROOT/../kama/out/$PLAT/kama.exe"
elif [ -x "$ROOT/../kama/out/$PLAT/kama" ]; then KAMA="$ROOT/../kama/out/$PLAT/kama"
else echo "check-bindings: no kama (set KAMA)" >&2; exit 1
fi

WGPU_DIR="${KAMA_WGPU_DIR:-$ROOT/third_party/wgpu/$PLAT}"
native_h="$WGPU_DIR/include/webgpu/webgpu.h"
extras_h="$WGPU_DIR/include/webgpu/wgpu.h"
# $EMSDK, else the emsdk that `emcc` on PATH sits in (<emsdk>/upstream/emscripten/emcc), else ~/emsdk.
if [ -n "${EMSDK:-}" ]; then EMSDK_DIR="$EMSDK"
elif command -v emcc >/dev/null 2>&1; then EMSDK_DIR="$(cd "$(dirname "$(command -v emcc)")/../.." && pwd)"
else EMSDK_DIR="$HOME/emsdk"
fi
web_h="$EMSDK_DIR/upstream/emscripten/cache/ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu/include/webgpu/webgpu.h"
[ -f "$native_h" ] || { echo "check-bindings: no wgpu-native header at $native_h (fetch the SDK; see the top of this file)" >&2; exit 1; }
[ -f "$web_h" ] || { echo "check-bindings: no emdawnwebgpu header at $web_h (build anything with \"webgpu\": true for the web once)" >&2; exit 1; }

out="$ROOT/out/check-bindings"
mkdir -p "$out"
"$KAMA" build tools/wgpugen/kama.json -o "$out/wgpugen" > "$out/build.log" 2>&1 || { cat "$out/build.log"; exit 1; }
"$out/wgpugen" "$native_h" "$extras_h" "$web_h" "$out/webgpu.gen.kama" "$out/constants.gen.c" > "$out/wgpugen.log" \
    || { cat "$out/wgpugen.log"; exit 1; }

committed="$ROOT/src/native/webgpu.gen.kama"
if [ "${1:-}" = "--write" ]; then
    cp "$out/webgpu.gen.kama" "$committed"
    echo "check-bindings: wrote src/native/webgpu.gen.kama ($(head -1 "$out/wgpugen.log" | sed 's/^wgpugen: //'))"
elif cmp -s "$out/webgpu.gen.kama" "$committed"; then
    echo "check-bindings: ok   the bindings are what the headers make"
else
    diff "$committed" "$out/webgpu.gen.kama" | head -20 || true
    echo "check-bindings: FAIL src/native/webgpu.gen.kama is not what tools/wgpugen makes (run with --write after a header moves)" >&2
    exit 1
fi

constants() {
    local leg="$1"; shift
    if "$@" -std=c11 -c "$out/constants.gen.c" -o "$out/constants-$leg.o" 2>"$out/constants-$leg.log"; then
        echo "check-bindings: ok   every constant matches the $leg header (C)"
    elif "$@" -x c++ -std=c++17 -c "$out/constants.gen.c" -o "$out/constants-$leg.o" 2>>"$out/constants-$leg.log"; then
        echo "check-bindings: ok   every constant matches the $leg header (C++: this compiler does not fold a C static const)"
    else
        grep -m 20 "error" "$out/constants-$leg.log" || cat "$out/constants-$leg.log"
        echo "check-bindings: FAIL a constant differs from the $leg header" >&2
        return 1
    fi
}

constants native clang -I "$WGPU_DIR/include"
if command -v emcc >/dev/null 2>&1; then
    constants web emcc --use-port=emdawnwebgpu
else
    echo "check-bindings: skip the web header's constants: no emcc on PATH (run from an interactive UCRT64 shell, bash -lic)"
fi
