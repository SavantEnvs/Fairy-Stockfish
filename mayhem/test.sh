#!/usr/bin/env bash
# mayhem/test.sh — known-answer perft suite (upstream tests/perft.sh positions, depths small enough
# for CI) run against the engine binary mayhem/build.sh built with normal flags (src/stockfish).
# Each case asserts the exact node count the engine prints for `go perft N`, plus a variant-config
# known answer (`load <<EOF` of a custom variant, then perft) and a FEN round trip.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

SF="$SRC/src/stockfish"
if [ ! -x "$SF" ]; then echo "missing $SF — run mayhem/build.sh" >&2; emit_ctrf perft 0 1; exit 1; fi

passed=0; failed=0
check() {  # name expected-regex commands
  local name="$1" want="$2" out
  out=$(printf '%s' "$3" | timeout 120 "$SF" 2>/dev/null)
  if grep -qE "$want" <<<"$out"; then passed=$((passed+1)); else failed=$((failed+1)); echo "FAIL: $name (want /$want/)"; fi
}
perft() {  # variant position depth nodes
  check "$1 $2 d$3" "^Nodes searched: $4\$" \
    "$(printf 'setoption name UCI_Variant value %s\nposition %s\ngo perft %s\nquit\n' "$1" "$2" "$3")"
}

perft chess startpos 1 20
perft chess startpos 3 8902
perft chess startpos 4 197281
perft chess "fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -" 3 97862
perft chess "fen 8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -" 4 43238
perft chess "fen r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1" 3 9467
perft chess "fen rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8" 3 62379
perft chess "fen r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10" 3 89890
perft losalamos startpos 5 191846
perft berolina "fen k7/6P1/8/8/8/2K2p2/4p3/8 w - - 0 1" 3 1983
perft pawnsideways startpos 3 10022
perft legan startpos 4 8138
perft cambodian "fen r1s1ks1r/3nm3/pppNpppp/3n4/5P2/PPPPPNPP/8/R1SKMS1R b DEe 0 0 5" 2 72
perft ai-wok "fen 8/8/8/2sp2k1/7p/3P4/6K1/7r w - - 0 1" 5 30055
perft sittuyin "fen 1k5K/3r2P1/8/8/8/8/8/8[] w - - 0 1" 5 68662
perft almost startpos 3 11895
perft chigorin startpos 3 11408
perft spartan "fen k6k/hh2Q2h/8/8/8/8/8/4K3 w - - 0 1" 3 6130
perft shatranj startpos 4 68122
perft racingkings "fen 6r1/2K5/5k2/8/3R4/8/8/8 w - - 0 1" 4 86041
perft kingofthehill "fen rnb2b1r/ppp2ppp/3k4/8/1PKp1pn1/3Pq3/PBP1P2P/RN1Q1B1R w - - 4 12" 3 19003
perft 3check "fen 7r/1p4p1/pk3p2/RN6/8/P5Pp/3p1P1P/4R1K1 w - - 1+3 1 39" 3 12407
perft atomic "fen r4b1r/2kb1N2/p2Bpnp1/8/2Pp3p/1P1PPP2/P5PP/R3K2R b KQ - 0 1" 2 148
perft atomar "fen 7r/6Pb/1np5/1p2kp2/P1P1n3/1P2KP1B/5N2/8 w - - 0 1" 3 24644
perft antichess startpos 4 153299
perft giveaway "fen 8/2p5/8/8/8/8/P7/8 w - - 0 1" 12 2557
perft horde startpos 4 23310
perft coregal "fen rn2kb1r/ppp1pppp/6q1/8/2PP2b1/5B2/PP3P1P/R1BQK1NR w KQkq - 1 9" 3 20421
perft knightmate startpos 4 139774
perft crazyhouse "fen 2k5/8/8/8/8/8/8/4K3[Qn] w - - 0 1" 3 88634
perft crazyhouse "fen r1bqk2r/pppp1ppp/2n1p3/4P3/1b1Pn3/2NB1N2/PPP2PPP/R1BQK2R[] b KQkq - 0 1" 3 58057
perft chessgi "fen 5Rp1/2p1Nb2/2B4k/6p1/8/P3PP2/1PPqR3/3R1BKn[QBNPPPPrrrnpp] b - - 1 48" 2 32816
perft grand "fen r8r/1nbqkcabn1/ppp2ppppp/3p6/4pP4/10/10/PPPPP1PPPP/1NBQKCABN1/R8R w - e7 0 3" 2 5768
perft xiangqi "fen 4kcP1N/8n/3rb4/9/9/9/9/3p1A3/4K4/5CB2 w - - 0 1" 4 92741
perft janggi "fen 1n1kaabn1/cr2N4/5C1c1/p1pNp3p/9/9/P1PbP1P1P/3r1p3/4A4/R1BA1KB1R b - - 0 1" 4 76763

# Variant config known answer: a custom variant loaded through the UCI here-doc must equal chess.
check "load <<END custom variant perft" "^Nodes searched: 8902\$" \
  "$(printf 'load <<END\n[mychess:chess]\nEND\nsetoption name UCI_Variant value mychess\nposition startpos\ngo perft 3\nquit\n')"
# FEN round trip through the `d` board display.
check "fen after castling move" "^Fen: r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R4RK1 b kq - 1 1\$" \
  "$(printf 'position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1 moves e1g1\nd\nquit\n')"

echo "perft: $passed passed, $failed failed"
emit_ctrf perft "$passed" "$failed"
