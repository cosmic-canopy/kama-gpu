#!/usr/bin/env bash
# tools/check-bindings.sh — the WebGPU bindings, and the safe API's TextureFormat, are what the headers make, and their
# numbers are the headers'.
#
#   tools/check-bindings.sh            # regenerate into out/, compare with src/native/webgpu.gen.kama and
#                                      # src/formats.gen.kama, check constants
#   tools/check-bindings.sh --write    # regenerate and replace both, and COVERAGE.md (after a header moves, the generator
#                                      # changes, or the safe API calls more of the header)
#   tools/check-bindings.sh --write --lower-coverage   # the same, when the safe API is to call fewer: a decision
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
"$out/wgpugen" "$native_h" "$extras_h" "$web_h" "$out/webgpu.gen.kama" "$out/constants.gen.c" "$out/formats.gen.kama" \
    > "$out/wgpugen.log" \
    || { cat "$out/wgpugen.log"; exit 1; }

committed="$ROOT/src/native/webgpu.gen.kama"
formats="$ROOT/src/formats.gen.kama"
if [ "${1:-}" = "--write" ]; then
    cp "$out/webgpu.gen.kama" "$committed"
    cp "$out/formats.gen.kama" "$formats"
    echo "check-bindings: wrote src/native/webgpu.gen.kama and src/formats.gen.kama ($(head -1 "$out/wgpugen.log" | sed 's/^wgpugen: //'))"
else
    if cmp -s "$out/webgpu.gen.kama" "$committed"; then
        echo "check-bindings: ok   the bindings are what the headers make"
    else
        diff "$committed" "$out/webgpu.gen.kama" | head -20 || true
        echo "check-bindings: FAIL src/native/webgpu.gen.kama is not what tools/wgpugen makes (run with --write after a header moves)" >&2
        exit 1
    fi
    if cmp -s "$out/formats.gen.kama" "$formats"; then
        echo "check-bindings: ok   TextureFormat is what the headers make"
    else
        diff "$formats" "$out/formats.gen.kama" | head -20 || true
        echo "check-bindings: FAIL src/formats.gen.kama is not what tools/wgpugen makes (run with --write after a header moves)" >&2
        exit 1
    fi
fi

# The coverage report, COVERAGE.md: which of the headers' functions the safe API calls (AGENTS.gpu.md, every call in
# webgpu.h before 1.0). kama answers where each binding and each seam wrapper is referenced, one analysis per file;
# wgpugen keeps the call sites outside src/native/ that are not import entries, follows each seam wrapper the safe API
# calls through csrc/gpuseam.c, and writes the report. The counts it heads with are a ratchet: --write refuses a lower
# one unless --lower-coverage is passed too, since lowering it is a decision.
# Each function a file declares, gated or not, as `--refs line:column` (1-based line, 0-based column, at its name).
asks() {
    awk '{ d = $0; sub(/^@compileFor\([^)]*\) /, "", d) }
         d ~ /^unsafe fn [^ (]+ [A-Za-z0-9_]+\(/ { split(d, w, " "); split(w[4], p, "("); printf " --refs %d:%d", NR, index($0, " " p[1] "(") }' "$1"
}
calledOf() { sed -n "s/^\*\*$2[^:]*: \([0-9][0-9]*\) of .*/\1/p" "$1" | head -1; }
# The rules first, on a fixture that holds each one whatever the package calls today (tools/wgpugen/tests/coverage):
# a call counts, an import entry or a reference inside src/native/ does not, a seam wrapper the safe API calls is
# followed through its C (a callback it registers and a one-line helper included), one it never calls is not, and a
# wgpu function named only in a C comment is not called.
fixture="$ROOT/tools/wgpugen/tests/coverage"
for f in webgpu.gen seam; do
    # shellcheck disable=SC2046  # one argument per question, by design
    "$KAMA" query "$fixture/kama.json" "$fixture/src/native/$f.kama" $(asks "$fixture/src/native/$f.kama") --json \
        > "$out/fixture-refs-$f.json" 2> "$out/fixture-refs-$f.log" \
        || { cat "$out/fixture-refs-$f.log"; echo "check-bindings: FAIL kama could not answer for the coverage fixture" >&2; exit 1; }
done
"$out/wgpugen" --coverage "$native_h" "$extras_h" "$web_h" "$out/fixture-refs-webgpu.gen.json" "$out/fixture-refs-seam.json" \
    "$fixture/csrc/gpuseam.c" "$out/fixture-COVERAGE.md" > "$out/fixture-coverage.log" \
    || { cat "$out/fixture-coverage.log"; echo "check-bindings: FAIL tools/wgpugen could not report on the coverage fixture" >&2; exit 1; }
wrong=""
for row in wgpuDeviceCreateBuffer:yes wgpuInstanceRequestAdapter:yes wgpuAdapterRelease:yes wgpuInstanceProcessEvents:yes \
           wgpuBufferGetSize:no wgpuQueueSubmit:no wgpuBufferMapAsync:no wgpuDeviceDestroy:no wgpuTextureDestroy:no; do
    grep -q "^| \`${row%%:*}\` | [^|]* | ${row##*:} |\$" "$out/fixture-COVERAGE.md" || wrong="$wrong ${row%%:*} (want ${row##*:})"
done
grep -q '^\*\*webgpu\.h: 4 of ' "$out/fixture-COVERAGE.md" || wrong="$wrong (want 4 called: $(calledOf "$out/fixture-COVERAGE.md" 'webgpu\.h'))"
if [ -n "$wrong" ]; then
    echo "check-bindings: FAIL the coverage report's rules, on tools/wgpugen/tests/coverage:$wrong" >&2
    exit 1
fi
echo "check-bindings: ok   the coverage report's rules hold on the fixture"

for f in webgpu.gen seam; do
    # shellcheck disable=SC2046  # one argument per question, by design
    "$KAMA" query kama.json "src/native/$f.kama" $(asks "src/native/$f.kama") --json > "$out/refs-$f.json" 2> "$out/refs-$f.log" \
        || { cat "$out/refs-$f.log"; echo "check-bindings: FAIL kama could not say where src/native/$f.kama's functions are called" >&2; exit 1; }
done
"$out/wgpugen" --coverage "$native_h" "$extras_h" "$web_h" "$out/refs-webgpu.gen.json" "$out/refs-seam.json" \
    "$ROOT/csrc/gpuseam.c" "$out/COVERAGE.md" > "$out/coverage.log" \
    || { cat "$out/coverage.log"; echo "check-bindings: FAIL tools/wgpugen could not write the coverage report" >&2; exit 1; }
report="$ROOT/COVERAGE.md"
if [ "${1:-}" = "--write" ]; then
    if [ -f "$report" ] && [ "${2:-}" != "--lower-coverage" ]; then
        for part in 'webgpu\.h' 'wgpu\.h'; do
            was=$(calledOf "$report" "$part"); now=$(calledOf "$out/COVERAGE.md" "$part")
            if [ -n "$was" ] && [ "${now:-0}" -lt "$was" ]; then
                echo "check-bindings: FAIL the safe API calls fewer of ${part//\\/}'s functions ($now) than COVERAGE.md says ($was). Lowering it is a decision: run with --write --lower-coverage, and say why in the commit" >&2
                exit 1
            fi
        done
    fi
    cp "$out/COVERAGE.md" "$report"
    echo "check-bindings: wrote COVERAGE.md ($(head -1 "$out/coverage.log" | sed 's/^wgpugen: //'))"
elif cmp -s "$out/COVERAGE.md" "$report"; then
    echo "check-bindings: ok   COVERAGE.md is what the safe API calls ($(head -1 "$out/coverage.log" | sed 's/^wgpugen: //'))"
else
    if [ -f "$report" ]; then diff "$report" "$out/COVERAGE.md" | head -20 || true; fi
    echo "check-bindings: FAIL COVERAGE.md is not what the safe API calls (run with --write when a call is added; a lower count is refused)" >&2
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
