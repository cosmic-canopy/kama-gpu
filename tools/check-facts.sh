#!/usr/bin/env bash
# tools/check-facts.sh — what wgpu-native does that the seam is built on, held case by case (tests/facts/wgpufacts.c;
# AGENTS.gpu.md item 2). A wgpu-native that changes any of it fails here by name, before it can turn into a
# use-after-free or a crash inside the package.
#
#   tools/check-facts.sh
#
# It needs a C compiler ($CC, default clang) and wgpu-native's SDK at $KAMA_WGPU_DIR (default
# third_party/wgpu/<platform>), and a GPU: with no adapter every case says skip. Natively only; the web's facts are
# measured when its leg runs in a browser (GPU-3).
#
# Each case runs in its own process. A plain case passes on exit 0. An abort case passes only when the process ends
# (an exit other than 0, 1, 2 and 77) AND the output holds the panic's own words, so a crash for another reason fails.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) PLAT="${MSYSTEM:-$(uname -s)}-$(uname -m)"; EXE=.exe ;;
    *) PLAT="$(uname -s)-$(uname -m)"; EXE= ;;
esac
WGPU_DIR="${KAMA_WGPU_DIR:-$ROOT/third_party/wgpu/$PLAT}"
CC="${CC:-clang}"

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "check-facts: skip no C compiler ($CC; set CC)"
    exit 0
fi
[ -d "$WGPU_DIR/lib" ] || { echo "check-facts: no wgpu-native SDK at $WGPU_DIR (see tools/check-bindings.sh)" >&2; exit 1; }

out="$ROOT/out/check-facts"
mkdir -p "$out"
link=(-L"$WGPU_DIR/lib" -lwgpu_native)
case "$PLAT" in
    Linux-*) link+=(-Wl,-rpath,"$WGPU_DIR/lib" -lm -ldl -lpthread) ;;
    Darwin-*) link+=(-Wl,-rpath,"$WGPU_DIR/lib") ;;
esac
if ! "$CC" -std=c11 -O0 -g -Wall -Werror tests/facts/wgpufacts.c -I"$WGPU_DIR/include" "${link[@]}" \
        -o "$out/wgpufacts$EXE" > "$out/build.log" 2>&1; then
    cat "$out/build.log"
    echo "check-facts: FAIL tests/facts/wgpufacts.c did not build" >&2
    exit 1
fi
export PATH="$WGPU_DIR/lib:$PATH"   # Windows finds wgpu_native.dll on PATH

failed=0

plain() {
    local name="$1"
    local log="$out/$name.log"
    local code=0
    "$out/wgpufacts$EXE" "$name" > "$log" 2>&1 || code=$?
    if [ "$code" = 77 ]; then
        echo "check-facts: skip $name: $(tail -1 "$log")"
    elif [ "$code" = 0 ]; then
        sed 's/^/check-facts: /' "$log"
    else
        sed 's/^/check-facts: /' "$log" | grep -v "^check-facts: ok" || true
        echo "check-facts: FAIL $name (exit $code)" >&2
        failed=1
    fi
}

# An abort case: the process must end, and say why in wgpu-native's words.
aborts() {
    local name="$1" fact="$2" words="$3"
    local log="$out/$name.log"
    local code=0
    "$out/wgpufacts$EXE" "$name" > "$log" 2>&1 || code=$?
    case "$code" in
        77) echo "check-facts: skip $name: $(tail -1 "$log")" ;;
        0|1|2) grep -m1 "FAIL" "$log" || true
               echo "check-facts: FAIL $fact: the process went on (exit $code)" >&2; failed=1 ;;
        *) if grep -qF -- "$words" "$log"; then
               echo "check-facts: ok   $fact (\"$words\")"
           else
               grep -m3 "panicked\|error" "$log" || tail -3 "$log"
               echo "check-facts: FAIL $fact: it ended (exit $code), but not saying \"$words\"" >&2; failed=1
           fi ;;
    esac
}

plain inthecall
plain twodevices
plain atfinish
plain afterrelease
plain noloss
plain wgsl
aborts abort-nohandler "a device with no uncaptured callback aborts on its first error" "wgpu uncaptured error"
aborts abort-internal "pushing an Internal error scope aborts" "invalid error filter"
aborts abort-emptypop "popping an error scope with none pushed aborts" 'called `Option::unwrap()` on a `None` value'
aborts abort-waitany "waiting through wgpuInstanceWaitAny aborts" "not implemented"
aborts abort-submit "submitting a command buffer whose finish failed aborts" "Error in wgpuQueueSubmit"
aborts abort-compilationinfo "asking a shader module for its compilation info aborts" "not implemented"

exit "$failed"
