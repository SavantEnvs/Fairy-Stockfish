#!/usr/bin/env bash
# mayhem/build.sh — build the Fairy-Stockfish fuzz harnesses and the upstream engine binary
# that mayhem/test.sh drives. Air-gapped (no fetches), idempotent (make + per-object rebuild).
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# 1) The engine sources (upstream's Makefile SRCS minus main.cpp), instrumented, with the
#    defines upstream's `make build ARCH=x86-64 largeboards=yes` uses (release build: -DNDEBUG,
#    as upstream's default debug=no). Objects land in mayhem/obj/ (*.o is gitignored upstream).
ENGINE_DEFS="-DLARGEBOARDS -DPRECOMPUTED_MAGICS -DNNUE_EMBEDDING_OFF -DUSE_PTHREADS -DIS_64BIT -DNDEBUG -msse -msse2 -DUSE_SSE2"
# SanCov on every engine/harness object (libFuzzer feedback on the engine, not just the harness)
: "${FUZZ_COV_FLAGS=-fsanitize=fuzzer-no-link}"
CXXF="-std=c++17 -O1 $ENGINE_DEFS $SANITIZER_FLAGS $FUZZ_COV_FLAGS $DEBUG_FLAGS"
OBJ="$SRC/mayhem/obj"
mkdir -p "$OBJ"
ENGINE_SRCS="benchmark.cpp bitbase.cpp bitboard.cpp endgame.cpp evaluate.cpp material.cpp misc.cpp
  movegen.cpp movepick.cpp pawns.cpp position.cpp psqt.cpp search.cpp thread.cpp timeman.cpp tt.cpp
  uci.cpp ucioption.cpp tune.cpp syzygy/tbprobe.cpp nnue/evaluate_nnue.cpp nnue/features/half_ka_v2.cpp
  partner.cpp parser.cpp piece.cpp variant.cpp xboard.cpp nnue/features/half_ka_v2_variants.cpp"
objs=()
for s in $ENGINE_SRCS; do objs+=("$OBJ/$(echo "$s" | tr / _ | sed 's/\.cpp$/.o/')"); done
export CXXF
i=0
for s in $ENGINE_SRCS; do printf '%s %s\n' "src/$s" "${objs[$i]}"; i=$((i+1)); done \
  | xargs -P "$MAYHEM_JOBS" -n 2 bash -c '$CXX $CXXF -c "$0" -o "$1"'

$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OBJ/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OBJ/standalone_main.o"

# 2) Harnesses: libFuzzer binary + standalone reproducer each.
build_harness() {  # name source [extra flags]
  local name="$1" src="$2"; shift 2
  $CXX $CXXF "$@" -I"$SRC/src" -c "$SRC/mayhem/harnesses/$src" -o "$OBJ/$name.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OBJ/$name.o" "${objs[@]}" "$OBJ/lsan_off.o" \
      -lpthread -lm -o "$SRC/$name"
  $CXX $SANITIZER_FLAGS $FUZZ_COV_FLAGS $DEBUG_FLAGS "$OBJ/standalone_main.o" "$OBJ/$name.o" "${objs[@]}" "$OBJ/lsan_off.o" \
      -lpthread -lm -o "$SRC/$name-standalone"
}
build_harness fuzz_fen fuzz_fen.cpp
build_harness fuzz_variant fuzz_variant.cpp
build_harness fuzz_san fuzz_san.cpp
build_harness fuzz_uci fuzz_protocol.cpp
build_harness fuzz_xboard fuzz_protocol.cpp -DFS_XBOARD
build_harness fuzz_nnue fuzz_nnue.cpp

# 2b) Syzygy tablebase target: a file-input AFL binary (tbprobe.cpp exit()s on corrupt tables),
#     engine + harness built with afl-clang-fast++ (AFL edge instrumentation instead of SanCov),
#     no -standalone (an AFL target is its own reproducer).
AFL_OBJ="$SRC/mayhem/obj-afl"
mkdir -p "$AFL_OBJ"
AFLCXXF="-std=c++17 -O1 $ENGINE_DEFS $SANITIZER_FLAGS $DEBUG_FLAGS"
export AFLCXXF AFL_QUIET=1
afl_objs=()
for s in $ENGINE_SRCS; do afl_objs+=("$AFL_OBJ/$(echo "$s" | tr / _ | sed 's/\.cpp$/.o/')"); done
i=0
for s in $ENGINE_SRCS; do printf '%s %s\n' "src/$s" "${afl_objs[$i]}"; i=$((i+1)); done \
  | xargs -P "$MAYHEM_JOBS" -n 2 bash -c 'afl-clang-fast++ $AFLCXXF -c "$0" -o "$1"'
afl-clang-fast++ $AFLCXXF -I"$SRC/src" -c "$SRC/mayhem/harnesses/fuzz_syzygy.cpp" -o "$AFL_OBJ/fuzz_syzygy.o"
afl-clang-fast++ $SANITIZER_FLAGS $DEBUG_FLAGS "$AFL_OBJ/fuzz_syzygy.o" "${afl_objs[@]}" "$OBJ/lsan_off.o" \
    -lpthread -lm -o "$SRC/fuzz_syzygy"

# 3) The upstream engine with its NORMAL flags (the oracle mayhem/test.sh drives): src/stockfish.
env -u CXXFLAGS -u LDFLAGS -u CFLAGS make -C "$SRC/src" -j"$MAYHEM_JOBS" build \
    ARCH=x86-64 COMP=clang largeboards=yes debug=no \
    EXTRACXXFLAGS="$COVERAGE_FLAGS" EXTRALDFLAGS="$COVERAGE_FLAGS"
