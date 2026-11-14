#!/usr/bin/env bash
# tools/test.sh — @kama/gpu's gate: the tests program built debug and release and run natively, a window cleared for
# 60 frames and a triangle drawn for 60 and read back from its window (both skipped when KAMA_WINDOW_NO_DISPLAY is set),
# the xfails refused, the tests and both examples built for the web and run in every
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
# printErrors (AGENTS.gpu.md item 12, decision 3, item 2): a tests run's output holds the two validation errors the
# printing device's channel takes, each printed once under its own header (Dawn's words name a label twice): one left to
# the channel, and one caught by a scope dropped without its pop. The error a popped scope caught is never printed. Every
# other device the tests open reads its own channel and prints nothing, so the run prints exactly these two.
printed() {
    local log="$1" leg="$2" headers validation left orphaned scoped
    headers="$(grep -c '^@kama/gpu: ' "$log" || true)"
    validation="$(grep -c '^@kama/gpu: a validation error: ' "$log" || true)"
    left="$(grep -c 'printed by default' "$log" || true)"
    orphaned="$(grep -c 'printed from an abandoned scope' "$log" || true)"
    scoped="$(grep -c 'never printed' "$log" || true)"
    if [ "$headers" = 2 ] && [ "$validation" = 2 ] && [ "$left" -ge 1 ] && [ "$orphaned" -ge 1 ] && [ "$scoped" = 0 ]; then
        echo "ok   printErrors printed the channel's two errors once each, and the popped scope's never ($leg)"
    else
        echo "FAIL printErrors ($leg): ${headers} printed (${validation} as validation errors), the channel's named ${left} and ${orphaned} times, the popped scope's ${scoped}"
        failed=1
    fi
}
# examples/triangle's own check (AGENTS.gpu.md item 12, decision 15): its last frame read back from its window, the
# centre the triangle's colour and a corner the clear colour. A run that skipped the read back, or never reached it,
# fails here, whatever its exit status.
readBack() {
    local log="$1" leg="$2"
    if grep -q '^triangle: the window reads back as drawn: ' "$log"; then
        echo "ok   the triangle reads back from its window as drawn ($leg)"
    else
        echo "FAIL the triangle never read back from its window as drawn ($leg)"
        failed=1
    fi
}
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
        "$out" 2>&1 | tee "tests/out/run-$mode.log" || failed=1
        printed "tests/out/run-$mode.log" "native, $mode"
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

step "a triangle drawn for 60 frames and read back from its window (examples/triangle)"
# GPU-4's done: every handle a resource its drop releases, so the example releases nothing by hand, and it reaches
# webgpu.h only through the package.
if ! ls examples/triangle/src/*.kama > /dev/null 2>&1; then
    echo "FAIL examples/triangle has no source"; failed=1
elif grep -nE '\.raw\(|adopt\(|wgpu[A-Z]|extern fn' examples/triangle/src/*.kama; then
    echo "FAIL examples/triangle releases or binds by hand (above)"; failed=1
else
    echo "ok   examples/triangle releases nothing by hand"
fi
"$KAMA" pkg install examples/triangle/kama.json > /dev/null || failed=1
if [ -n "${KAMA_WINDOW_NO_DISPLAY:-}" ]; then
    echo "skip KAMA_WINDOW_NO_DISPLAY is set"
elif "$KAMA" build examples/triangle/kama.json -o "tests/out/triangle$EXE" > tests/out/build-triangle.log 2>&1; then
    "tests/out/triangle$EXE" --frames 60 2>&1 | tee tests/out/run-triangle.log || failed=1
    readBack tests/out/run-triangle.log "native"
else
    tail -20 tests/out/build-triangle.log
    failed=1
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

step "xfail: one flag set where another is wanted, and an empty set"
xf=tests/xfail/usage_mixed
"$KAMA" pkg install "$xf/kama.json" > /dev/null
if "$KAMA" check "$xf/kama.json" > "tests/out/xfail-usage.log" 2>&1; then
    echo "FAIL the mixed flag sets compiled"; failed=1
elif [ "$(grep -c "these are unrelated types" tests/out/xfail-usage.log)" = 3 ] \
        && grep -q "fromBits.*not visible\|fromBits.*private" tests/out/xfail-usage.log; then
    echo "ok   each flag set is its own type: 3 mixes refused as unrelated types, and no empty set can be made"
else
    cat tests/out/xfail-usage.log; echo "FAIL refused, but not as 3 unrelated-type mixes and a private fromBits"; failed=1
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
elif ! "$KAMA" build examples/triangle/kama.json --target WASM -o tests/out/triangle-web/triangle.html > tests/out/build-triangle-web.log 2>&1; then
    tail -20 tests/out/build-triangle-web.log
    failed=1
elif ! command -v node >/dev/null 2>&1; then
    echo "ok   built tests/out/web/gputests.html, tests/out/clear-web/clear.html and tests/out/triangle-web/triangle.html"
    echo "skip no node on PATH, so no browser run"
else
    # Under `kama run` (AGENTS.gpu.md item 11), bounded since it has no time limit of its own: Edge and Chrome as
    # installed, and the pinned Firefox, whose headless page has WebGPU since kama 0.9.940 sets its pref (kyoten's
    # KY-54, fixed), so every leg runs every case, the printErrors check and the clear. Firefox differs from Dawn in
    # five ways its run passes: it has no immediates, so maxImmediateSize is held to 0 (--no-immediates); its WGSL
    # compiler is naga, which gives no warnings (--no-shader-warnings); a buffer of 2^53 - 256 bytes is an OutOfMemory
    # error there (--huge-buffer-out-of-memory); its render pass errors never name the pass (--unnamed-pass-errors); and
    # it takes a depth slice of 2^32 - 2 on a 2D view (--unchecked-depth-slice).
    if browsers="$(node tools/browsers.mjs run 2>/dev/null)"; then
        while IFS= read -r browser; do
            name="${browser##*[/\\]}"
            differences=()
            if [[ "${name,,}" == *firefox* ]]; then
                differences=(--no-immediates --no-shader-warnings --huge-buffer-out-of-memory --unnamed-pass-errors --unchecked-depth-slice)
            fi
            echo "-- kama run, $browser ${differences[*]}"
            KAMA_BROWSER="$browser" bounded "$KAMA" run tests/kama.json --target WASM --headless -- "${differences[@]}" 2>&1 | tee tests/out/run-web.log || failed=1
            printed tests/out/run-web.log "$name"
            echo "-- kama run, $browser: examples/clear on #canvas for 60 frames"
            KAMA_BROWSER="$browser" bounded "$KAMA" run examples/clear/kama.json --target WASM --headless -- --frames 60 || failed=1
            echo "-- kama run, $browser: examples/triangle on #canvas for 60 frames, read back"
            KAMA_BROWSER="$browser" bounded "$KAMA" run examples/triangle/kama.json --target WASM --headless -- --frames 60 2>&1 | tee tests/out/run-triangle-web.log || failed=1
            readBack tests/out/run-triangle-web.log "$name"
        done <<< "$browsers"
    else
        echo "skip no browser for kama run (Edge or Chrome installed, or Firefox pinned under ~/browsers; or set KAMA_BROWSER)"
    fi
    # The pinned Chrome for Testing, through the stopgap driver: it never loads a page under `kama run` (kyoten's KY-53).
    if browsers="$(node tools/browsers.mjs driver 2>/dev/null)"; then
        while IFS= read -r browser; do
            echo "-- the driver, $browser"
            node tools/webtest.mjs tests/out/web gputests.html --browser "$browser" 2>&1 | tee tests/out/run-driver.log || failed=1
            printed tests/out/run-driver.log "the driver"
            echo "-- the driver, $browser: examples/clear on #canvas for 60 frames"
            node tools/webtest.mjs tests/out/clear-web "clear.html?arg=--frames&arg=60" --browser "$browser" || failed=1
            echo "-- the driver, $browser: examples/triangle on #canvas for 60 frames, read back"
            node tools/webtest.mjs tests/out/triangle-web "triangle.html?arg=--frames&arg=60" --browser "$browser" 2>&1 | tee tests/out/run-triangle-driver.log || failed=1
            readBack tests/out/run-triangle-driver.log "the driver"
        done <<< "$browsers"
    else
        echo "skip no Chrome for Testing pinned under ~/browsers, so no driver run"
    fi
fi

echo
if [ "$failed" = 0 ]; then echo "test: all passed"; else echo "test: FAILED" >&2; fi
exit "$failed"
