#!/bin/sh
# check-roadmap.sh — ROADMAP.md stays the short ordered list; each row's reasoning stays where it links.
#
# Ported from kama's tools/check-roadmap.sh, as friendly-fire-department's is, with FFD's per-row ceiling. ROADMAP.md
# answers "what do we do next", and only while that answer is readable in one screen: kama's and FFD's each passed a
# thousand lines once by absorbing their reasoning, and FFD's grew sideways along single rows once a line ceiling held
# its length. So the invariants are structural:
#   1. ROADMAP.md is under a line ceiling, and every row under a character ceiling.
#   2. Every `ROADMAP_DETAIL.md#anchor` link in ROADMAP.md resolves to an anchor that exists.
#   3. Every anchor in ROADMAP_DETAIL.md is reached by some row: detail nothing schedules is a changelog.
#   4. Every row id is `GPU-<n>`, and unique. An id is permanent: never reused, never renumbered.
#   5. Every `GPU-<n>` cited in either file names a row that exists.
#   6. The `Next id:` counter is above every id ROADMAP.md has ever issued, its own history included.
set -u

ID=GPU
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RM="$ROOT/ROADMAP.md"
RD="$ROOT/ROADMAP_DETAIL.md"

# Drift alarms, not style rules. Raise one only with a reason.
MAX_LINES=120
MAX_ROW_CHARS=400

fail() { echo "check-roadmap: FAIL — $1" >&2; exit 1; }

[ -f "$RM" ] || fail "no ROADMAP.md"
[ -f "$RD" ] || fail "no ROADMAP_DETAIL.md"

# ---- 1. the ceilings ------------------------------------------------------------------------------
lines=$(wc -l < "$RM" | tr -d ' ')
[ "$lines" -le "$MAX_LINES" ] || fail "ROADMAP.md is $lines lines (ceiling $MAX_LINES). It is the ORDER, not the reasoning: move the prose into ROADMAP_DETAIL.md and leave a one-line row linking to it."
long=$(LC_ALL=C awk -v max="$MAX_ROW_CHARS" -v id="$ID" '$0 ~ "^[|] " id "-[0-9]+ [|]" && length($0) > max { print "    " NR ": " length($0) " characters" }' "$RM")
[ -z "$long" ] || { echo "check-roadmap: FAIL — a row is over $MAX_ROW_CHARS characters; its prose belongs in ROADMAP_DETAIL.md:" >&2; echo "$long" >&2; exit 1; }

# ---- 2. every link resolves; 3. no orphaned detail ------------------------------------------------
want=$(grep -o 'ROADMAP_DETAIL\.md#[A-Za-z0-9_-]*' "$RM" | sed 's/.*#//' | sort -u)
have=$(grep -o '<a id="[A-Za-z0-9_-]*"' "$RD" | sed 's/.*id="//; s/"//' | sort -u)
for a in $want; do printf '%s\n' "$have" | grep -qx "$a" || fail "ROADMAP.md links to #$a, which ROADMAP_DETAIL.md does not have"; done
for a in $have; do printf '%s\n' "$want" | grep -qx "$a" || fail "ROADMAP_DETAIL.md's #$a is scheduled by no row: add the row, or delete the section"; done

# ---- 4. every row id well-formed and unique -------------------------------------------------------
ids=$(LC_ALL=C grep -oE "^\| $ID-[0-9]+ \|" "$RM" | tr -d '| ')
rows=$(printf '%s\n' "$ids" | grep -c .)
dupes=$(printf '%s\n' "$ids" | sort | uniq -d)
[ -z "$dupes" ] || fail "a row id is used twice: $(echo $dupes). Give the newer row the next id; never reuse or renumber one."
legacy=$(LC_ALL=C grep -nE '^\| [0-9]+ \|' "$RM" || true)
[ -z "$legacy" ] || fail "a row is numbered by position instead of carrying a \`$ID-\` id: $legacy"

# ---- 5. every citation names a row ----------------------------------------------------------------
bad=$(LC_ALL=C grep -vhE "^\*\*Next id: $ID-" "$RM" "$RD" | grep -oE "$ID-[0-9]+" | sort -u | while read -r ref; do
    printf '%s\n' "$ids" | grep -qx "$ref" || echo "$ref"
done)
[ -z "$bad" ] || fail "cited, but no row has the id: $(echo $bad). A shipped row's record is the SPEC or the git log; cite that instead."

# ---- 6. the counter is above every id ever issued -------------------------------------------------
next=$(LC_ALL=C sed -nE "s/^\*\*Next id: $ID-([0-9]+)\*\*\$/\1/p" "$RM")
[ -n "$next" ] || fail "ROADMAP.md has no \`**Next id: $ID-<n>**\` line"
[ "$(printf '%s\n' "$next" | grep -c .)" -eq 1 ] || fail "ROADMAP.md has more than one \`Next id:\` line"
highest=$( {
    printf '%s\n' "$ids"
    git -C "$ROOT" log -p --format= -- ROADMAP.md 2>/dev/null | LC_ALL=C grep -E "^[-+]\| $ID-[0-9]+ \|"
} | LC_ALL=C grep -oE "$ID-[0-9]+" | sed "s/$ID-//" | sort -n | tail -1)
if [ -n "$highest" ] && [ "$next" -le "$highest" ]; then
    fail "\`Next id: $ID-$next\` is not above $ID-$highest, which is already issued. Set it to $ID-$((highest + 1))."
fi

n=$(printf '%s\n' "$want" | grep -c .)
echo "check-roadmap: OK (ROADMAP.md $lines/$MAX_LINES lines, rows under $MAX_ROW_CHARS characters; $rows rows, ids unique;"
echo "                   $n detail sections, all linked; every \`$ID-\` citation resolves; next id $ID-$next is unissued)"
