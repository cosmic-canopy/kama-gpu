#!/usr/bin/env bash
# tools/test.sh — @kama/gpu's gate: the tests program built debug and release and run natively, a window cleared for
# 60 frames (skipped when KAMA_WINDOW_NO_DISPLAY is set), the xfail refused, the tests built for the web and run in every
# headless browser present, the bindings checked against the headers, and what wgpu-native does that the seam is built
# on (tools/check-facts.sh).
#
#   tools/test.sh
#
# Run it from msys2 UCRT64 on Windows (bash -lic, so emcc and node are on PATH for the web leg), or any shell elsewhere.
# It needs what tools/check-bindings.sh needs: kama, and wgpu-native's SDK at $KAMA_WGPU_DIR (default
# third_party/wgpu/<platform>). The web leg is built when `emcc` is on PATH and run, headless, in every browser
# tools/browsers.mjs finds (AGENTS.gpu.md item 11): under `kama run --target WASM` in Edge and Chrome as installed and in
# the pinned Firefox, and through tools/webtest.mjs in the pinned Chrome for Testing (KAMA_BROWSER names one). Each part
# is skipped, and says so, without what it needs.
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
# A browser run, bounded where `timeout` exists: `kama run` has no time limit, and a browser that never asks for the page
# waits for ever (kyoten's KY-53).
bounded() { if command -v timeout >/dev/null 2>&1; then timeout 300 "$@"; else "$@"; fi; }

step "the roadmap"
tools/check-roadmap.sh || failed=1

step "the bindings"
tools/check-bindings.sh || failed=1

step "wgpu-native's facts (tests/facts)"
tools/check-facts.sh || failed=1

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

step "the web leg: built, and run in every headless browser present"
if ! command -v emcc >/dev/null 2>&1; then
    echo "skip no emcc on PATH (run from an interactive UCRT64 shell, bash -lic)"
elif ! "$KAMA" build tests/kama.json --target WASM -o tests/out/web/gputests.html > tests/out/build-web.log 2>&1; then
    tail -20 tests/out/build-web.log
    failed=1
elif ! "$KAMA" build examples/clear/kama.json --target WASM -o tests/out/clear-web/clear.html > tests/out/build-clear-web.log 2>&1; then
    tail -20 tests/out/build-clear-web.log
    failed=1
elif ! command -v node >/dev/null 2>&1; then
    echo "ok   built tests/out/web/gputests.html and tests/out/clear-web/clear.html"
    echo "skip no node on PATH, so no browser run"
else
    # Under `kama run` (AGENTS.gpu.md item 11), bounded since it has no time limit of its own: Edge and Chrome as
    # installed, and the pinned Firefox, which gives WebGPU no adapter here, so its run says so and skips the cases that
    # need one (--adapter-optional), and the clear, which needs one, is not run in it.
    if browsers="$(node tools/browsers.mjs run 2>/dev/null)"; then
        while IFS= read -r browser; do
            name="${browser##*[/\\]}"
            if [[ "${name,,}" == *firefox* ]]; then
                echo "-- kama run, $browser (no WebGPU adapter here: the cases that need one say so)"
                KAMA_BROWSER="$browser" bounded "$KAMA" run tests/kama.json --target WASM --headless -- --adapter-optional || failed=1
            else
                echo "-- kama run, $browser"
                KAMA_BROWSER="$browser" bounded "$KAMA" run tests/kama.json --target WASM --headless || failed=1
                echo "-- kama run, $browser: examples/clear on #canvas for 60 frames"
                KAMA_BROWSER="$browser" bounded "$KAMA" run examples/clear/kama.json --target WASM --headless -- --frames 60 || failed=1
            fi
        done <<< "$browsers"
    else
        echo "skip no browser for kama run (Edge or Chrome installed, or Firefox pinned under ~/browsers; or set KAMA_BROWSER)"
    fi
    # The pinned Chrome for Testing, through the stopgap driver: it never loads a page under `kama run` (kyoten's KY-53).
    if browsers="$(node tools/browsers.mjs driver 2>/dev/null)"; then
        while IFS= read -r browser; do
            echo "-- the driver, $browser"
            node tools/webtest.mjs tests/out/web gputests.html --browser "$browser" || failed=1
            echo "-- the driver, $browser: examples/clear on #canvas for 60 frames"
            node tools/webtest.mjs tests/out/clear-web "clear.html?arg=--frames&arg=60" --browser "$browser" || failed=1
        done <<< "$browsers"
    else
        echo "skip no Chrome for Testing pinned under ~/browsers, so no driver run"
    fi
fi

echo
if [ "$failed" = 0 ]; then echo "test: all passed"; else echo "test: FAILED" >&2; fi
exit "$failed"
