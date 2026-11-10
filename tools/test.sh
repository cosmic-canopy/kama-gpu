#!/usr/bin/env bash
# tools/test.sh — @kama/gpu's gate: the tests program built debug and release and run natively, a window cleared for
# 60 frames (skipped when KAMA_WINDOW_NO_DISPLAY is set), the xfail refused, the
# wasm leg built, and the bindings checked against the headers.
#
#   tools/test.sh
#
# Run it from msys2 UCRT64 on Windows (bash -lic, so emcc is on PATH for the wasm leg), or any shell elsewhere. It
# needs what tools/check-bindings.sh needs: kama, and wgpu-native's SDK at $KAMA_WGPU_DIR (default
# third_party/wgpu/<platform>). The wasm leg is built when `emcc` is on PATH and skipped, said so, without; running it
# needs a browser with WebGPU, which is not wired here yet.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) PLAT="${MSYSTEM:-$(uname -s)}-$(uname -m)"; EXE=.exe ;;
    *) PLAT="$(uname -s)-$(uname -m)"; EXE= ;;
esac
if [ -n "${KAMA:-}" ]; then :
elif command -v kama >/dev/null 2>&1; then KAMA=kama
elif [ -x "$ROOT/../kama/out/$PLAT/kama$EXE" ]; then KAMA="$ROOT/../kama/out/$PLAT/kama$EXE"
else echo "test: no kama (set KAMA)" >&2; exit 1
fi
export KAMA_WGPU_DIR="${KAMA_WGPU_DIR:-$ROOT/third_party/wgpu/$PLAT}"
[ -d "$KAMA_WGPU_DIR/lib" ] || { echo "test: no wgpu-native SDK at $KAMA_WGPU_DIR (see tools/check-bindings.sh)" >&2; exit 1; }
# the native leg links wgpu_native dynamically: the loader finds it on PATH (Windows) or the rpath kama sets
export PATH="$KAMA_WGPU_DIR/lib:$PATH"

failed=0
step() { echo; echo "== $*"; }

step "the roadmap"
tools/check-roadmap.sh || failed=1

step "the bindings"
tools/check-bindings.sh || failed=1

step "kama check"
"$KAMA" check kama.json || failed=1
"$KAMA" pkg install tests/kama.json > /dev/null
"$KAMA" check tests/kama.json || failed=1

for mode in debug release; do
    step "native, $mode"
    flag=""
    [ "$mode" = release ] && flag="--release"
    out="tests/out/native-$mode/gputests$EXE"
    if "$KAMA" build tests/kama.json $flag -o "$out" > "tests/out/build-$mode.log" 2>&1; then
        "$out" || failed=1
    else
        tail -20 "tests/out/build-$mode.log"
        failed=1
    fi
done

step "a window cleared for 60 frames (examples/clear)"
if [ -n "${KAMA_WINDOW_NO_DISPLAY:-}" ]; then
    echo "skip KAMA_WINDOW_NO_DISPLAY is set"
else
    "$KAMA" pkg install examples/clear/kama.json > /dev/null
    if "$KAMA" build examples/clear/kama.json -o "tests/out/clear$EXE" > tests/out/build-clear.log 2>&1; then
        "tests/out/clear$EXE" --frames 60 || failed=1
    else
        tail -20 tests/out/build-clear.log
        failed=1
    fi
fi

step "xfail: a moved-from encoder"
xf=tests/xfail/encoder_after_finish
"$KAMA" pkg install "$xf/kama.json" > /dev/null
if "$KAMA" check "$xf/kama.json" > "tests/out/xfail.log" 2>&1; then
    echo "FAIL the second finish() compiled"; failed=1
elif grep -q "moved" "tests/out/xfail.log"; then
    echo "ok   the second finish() is refused: $(grep -m1 "moved" tests/out/xfail.log | sed 's/^.*error: //')"
else
    cat tests/out/xfail.log; echo "FAIL refused, but not as a use after move"; failed=1
fi

step "wasm leg (build)"
if command -v emcc >/dev/null 2>&1; then
    "$KAMA" build tests/kama.json --target WASM -o tests/out/wasm/gputests.html > tests/out/build-wasm.log 2>&1 \
        && echo "ok   built tests/out/wasm/gputests.html" \
        || { tail -20 tests/out/build-wasm.log; failed=1; }
else
    echo "skip no emcc on PATH (run from an interactive UCRT64 shell, bash -lic)"
fi

echo
if [ "$failed" = 0 ]; then echo "test: all passed"; else echo "test: FAILED" >&2; fi
exit "$failed"
