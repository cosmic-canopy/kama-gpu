#!/usr/bin/env bash
# tools/test.sh — @kama/gpu's gate: the tests program built debug and release and run natively, a window cleared for
# 60 frames and a triangle drawn for 60 and read back from its window (both skipped when KAMA_WINDOW_NO_DISPLAY is set),
# the xfails refused, the tests and both examples built for the web, debug and release, and run in every headless browser
# present, a release build's JS held to Closure's renaming (tools/closure-names.mjs), the bindings checked against the
# headers, and what wgpu-native does that the seam is built on (tools/check-facts.sh).
#
#   tools/test.sh
#
# Run it from msys2 UCRT64 on Windows (bash -lic, so emcc and node are on PATH for the web leg), or any shell elsewhere.
# It needs what tools/check-bindings.sh needs: kama, and wgpu-native's SDK at $KAMA_WGPU_DIR (default
# third_party/wgpu/<platform>). The web leg is built when `emcc` is on PATH and run, headless, under
# `kama run --target WASM` in every browser tools/browsers.mjs finds (AGENTS.gpu.md item 11): Edge and Chrome as
# installed, the pinned Firefox and the pinned Chrome for Testing (KAMA_BROWSER names one). Each part is skipped, and says
# so, without what it needs.
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

# Every leg runs a debug build and a release build, natively and on the web (kyoten's rendering.md § Decided,
# 2026-11-17): a wasm release runs Closure over the page's JS since kama 0.9.966 (KR-289), which renames every name it
# cannot prove the browser's, and a game ships that build.
#
# A tests run's first line is what its program was built as (`build: debug` or `build: release`), so a leg that lost its
# `--release` fails here rather than passing as the leg it names.
builtAs() {
    local log="$1" mode="$2" leg="$3"
    if tr -d '\r' < "$log" | grep -qx "build: $mode"; then
        echo "ok   $leg ran a $mode build"
    else
        echo "FAIL $leg: its program never said \"build: $mode\", so the leg ran another build"
        failed=1
    fi
}
# A release build's JS that still names emscripten's `UTF8ToString` was never run through Closure: a root manifest's
# `"release": { "closure": false }` would do that, and the leg would test nothing of what a game ships.
closured() {
    local js="$1" leg="$2"
    if grep -q 'UTF8ToString' "$js"; then
        echo "FAIL $leg: $js still names UTF8ToString, so Closure never ran over it"
        failed=1
    else
        echo "ok   $leg: Closure ran over $js"
    fi
}

step "the roadmap"
tools/check-roadmap.sh || failed=1

step "the bindings"
tools/check-bindings.sh || failed=1

step "wgpu-native's facts (tests/facts)"
tools/check-facts.sh || failed=1

step "kama check"
# A fresh clone has neither its output folder nor any manifest's dependencies installed.
mkdir -p tests/out
for manifest in kama.json examples/clear/kama.json examples/triangle/kama.json; do
    "$KAMA" pkg install "$manifest" > /dev/null || failed=1
done
"$KAMA" check kama.json || failed=1
"$KAMA" pkg install tests/kama.json > /dev/null
"$KAMA" check tests/kama.json || failed=1

# A native leg with no fallback adapter (GPU-15): Metal has none, so on macOS the tests hold the request to NoAdapter.
nativeFlags=()
[ "$(uname -s)" = Darwin ] && nativeFlags=(--no-fallback-adapter)

for mode in debug release; do
    step "native, $mode"
    flag=""
    [ "$mode" = release ] && flag="--release"
    out="tests/out/native-$mode/gputests$EXE"
    if "$KAMA" build tests/kama.json $flag -o "$out" > "tests/out/build-$mode.log" 2>&1; then
        "$out" "${nativeFlags[@]}" 2>&1 | tee "tests/out/run-$mode.log" || failed=1
        printed "tests/out/run-$mode.log" "native, $mode"
        builtAs "tests/out/run-$mode.log" "$mode" "native, $mode"
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

step "the web leg: built debug and release, and run in every headless browser present"
# Each program built for the web in both builds, so a build that fails, as a release build Closure refuses does, fails the
# gate even with no browser to run it in.
webBuild() {
    local manifest="$1" out="$2" mode="$3" flag=""
    [ "$mode" = release ] && flag="--release"
    mkdir -p "$(dirname "$out")"
    if ! "$KAMA" build "$manifest" $flag --target WASM -o "$out" > "${out%.html}-build.log" 2>&1; then
        tail -20 "${out%.html}-build.log"
        echo "FAIL $manifest did not build for the web, $mode"
        failed=1
        return 1
    fi
}
if ! command -v emcc >/dev/null 2>&1; then
    echo "skip no emcc on PATH (run from an interactive UCRT64 shell, bash -lic)"
else
    for mode in debug release; do
        webBuild tests/kama.json "tests/out/web-$mode/gputests.html" "$mode" || true
        webBuild examples/clear/kama.json "tests/out/clear-web-$mode/clear.html" "$mode" || true
        webBuild examples/triangle/kama.json "tests/out/triangle-web-$mode/triangle.html" "$mode" || true
    done
    # The release builds' JS: each run through Closure, and every name js/gpuseam.js hands the browser kept in the tests'.
    for js in tests/out/web-release/gputests.js tests/out/clear-web-release/clear.js tests/out/triangle-web-release/triangle.js; do
        [ -f "$js" ] && closured "$js" "the release build"
    done
    if [ -f tests/out/web-release/gputests.js ] && command -v node >/dev/null 2>&1; then
        node tools/closure-names.mjs js/gpuseam.js tests/out/web-release/gputests.js || failed=1
    fi
    if ! command -v node >/dev/null 2>&1; then
        echo "skip no node on PATH, so no browser run"
    elif browsers="$(node tools/browsers.mjs run 2>/dev/null)"; then
        # Under `kama run` (AGENTS.gpu.md item 11), bounded since it has no time limit of its own: Edge and Chrome as
        # installed, and the pinned Firefox, whose headless page has WebGPU since kama 0.9.940 sets its pref (kyoten's
        # KY-54, fixed), so every leg runs every case, the printErrors check and the clear. Firefox differs from Dawn in
        # five ways its run passes: it has no immediates, so maxImmediateSize is held to 0 (--no-immediates); its WGSL
        # compiler is naga, which gives no warnings (--no-shader-warnings); a buffer of 2^53 - 256 bytes is an OutOfMemory
        # error there (--huge-buffer-out-of-memory); its render pass errors never name the pass (--unnamed-pass-errors);
        # and it takes a depth slice of 2^32 - 2 on a 2D view (--unchecked-depth-slice).
        #
        # Chromium offers WebGPU its fallback adapter (GPU-15) only when its command line asks: Edge with
        # --enable-unsafe-webgpu, Chrome with that and --enable-unsafe-swiftshader. The pinned Chrome for Testing is
        # started with both (tools/browsers.mjs flags, decided with the owner on 2026-11-14), so it runs the fallback
        # cases; Edge and an installed Chrome run as a player's do, on their real GPU, and offer none
        # (--no-fallback-adapter).
        #
        # Each browser runs the tests and both examples in a debug build, then in a release build, each run held to the
        # build its program says it is.
        cft=0
        while IFS= read -r browser; do
            name="${browser##*[/\\]}"
            flags=()
            while IFS= read -r f; do [ -n "$f" ] && flags+=("$f"); done < <(node tools/browsers.mjs flags "$browser" | tr -d '\r')
            # A leg's differences follow the browser, never its flags: a Chrome for Testing started without the flags
            # that offer the fallback adapter fails its fallback cases rather than skipping them.
            case "$(node tools/browsers.mjs kind "$browser" | tr -d '\r')" in
            firefox)
                differences=(--no-immediates --no-shader-warnings --huge-buffer-out-of-memory --unnamed-pass-errors --unchecked-depth-slice) ;;
            chrome-for-testing) differences=(); cft=1 ;;
            *) differences=(--no-fallback-adapter) ;;
            esac
            for mode in debug release; do
                flag=""
                [ "$mode" = release ] && flag="--release"
                echo "-- kama run, $browser, $mode${flags[*]:+ ${flags[*]}}${differences[*]:+ -- ${differences[*]}}"
                KAMA_BROWSER="$browser" bounded "$KAMA" run tests/kama.json $flag --target WASM --headless ${flags[@]+"${flags[@]}"} -- ${differences[@]+"${differences[@]}"} 2>&1 | tee "tests/out/run-web-$mode.log" || failed=1
                printed "tests/out/run-web-$mode.log" "$name, $mode"
                builtAs "tests/out/run-web-$mode.log" "$mode" "kama run, $name, $mode"
                echo "-- kama run, $browser, $mode: examples/clear on #canvas for 60 frames"
                KAMA_BROWSER="$browser" bounded "$KAMA" run examples/clear/kama.json $flag --target WASM --headless ${flags[@]+"${flags[@]}"} -- --frames 60 2>&1 | tee "tests/out/run-clear-web-$mode.log" || failed=1
                builtAs "tests/out/run-clear-web-$mode.log" "$mode" "examples/clear, $name, $mode"
                echo "-- kama run, $browser, $mode: examples/triangle on #canvas for 60 frames, read back"
                KAMA_BROWSER="$browser" bounded "$KAMA" run examples/triangle/kama.json $flag --target WASM --headless ${flags[@]+"${flags[@]}"} -- --frames 60 2>&1 | tee "tests/out/run-triangle-web-$mode.log" || failed=1
                readBack "tests/out/run-triangle-web-$mode.log" "$name, $mode"
                builtAs "tests/out/run-triangle-web-$mode.log" "$mode" "examples/triangle, $name, $mode"
            done
        done <<< "$browsers"
        [ "$cft" = 1 ] || echo "skip no Chrome for Testing pinned under ~/browsers, so no Chromium leg ran the fallback cases"
    else
        echo "skip no browser for kama run (Edge or Chrome installed, Firefox or Chrome for Testing pinned under ~/browsers; or set KAMA_BROWSER)"
    fi
fi

echo
if [ "$failed" = 0 ]; then echo "test: all passed"; else echo "test: FAILED" >&2; fi
exit "$failed"
