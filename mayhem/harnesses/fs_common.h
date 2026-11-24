// Shared one-time engine setup for the Fairy-Stockfish fuzz harnesses.
// Mirrors src/main.cpp's init sequence (everything before UCI::loop).
#pragma once
#include <cstdint>
#include <cstring>
#include <iostream>
#include <set>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

#include "bitboard.h"
#include "endgame.h"
#include "evaluate.h"
#include "misc.h"
#include "movegen.h"
#include "piece.h"
#include "position.h"
#include "psqt.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "tune.h"
#include "uci.h"
#include "variant.h"
#include "apiutil.h"

using namespace Stockfish;

namespace fsfuzz {

// Discards engine chatter on stdout (UCI/XBoard replies); stderr is untouched.
struct NullBuf : std::streambuf {
  int overflow(int c) override { return c == EOF ? 0 : c; }
  std::streamsize xsputn(const char*, std::streamsize n) override { return n; }
};

inline std::vector<std::string>& builtin_variants() {
  static std::vector<std::string> v;
  return v;
}

inline void init_engine() {
  static bool done = false;
  if (done) return;
  done = true;
  static NullBuf nb;
  std::cout.rdbuf(&nb);
  char arg0[] = "fairy-stockfish";
  char* argv[] = {arg0, nullptr};
  pieceMap.init();
  variants.init();
  CommandLine::init(1, argv);
  UCI::init(Options);
  Tune::init();
  PSQT::init(variants.find(Options["UCI_Variant"])->second);
  Bitboards::init();
  Position::init();
  Bitbases::init();
  Endgames::init();
  Threads.set(1);
  Options["Hash"] = std::string("1");   // small TT: Search::clear() per input stays cheap (needs threads up)
  Search::clear();
  Eval::NNUE::init();
  builtin_variants() = variants.get_keys();
}

// Removes every variant an input registered (a section can never overwrite an
// existing name: VariantMap::parse_istream rejects duplicates), so the next
// input sees exactly the built-in variant set.
inline void drop_added_variants() {
  std::set<std::string> keep(builtin_variants().begin(), builtin_variants().end());
  std::vector<std::string> extra;
  for (auto const& e : variants)
    if (!keep.count(e.first)) extra.push_back(e.first);
  for (auto const& k : extra) {
    delete variants[k];
    variants.erase(k);
  }
}

// Exercises a set-up position the way the binding API and search do:
// legal-move generation, check detection, do/undo, game-end rules, FEN output.
inline void exercise_position(Position& pos, bool eval) {
  (void)pos.fen();
  (void)pos.fen(true, true);
  Value r;
  (void)pos.is_immediate_game_end(r);
  (void)pos.is_optional_game_end(r);
  (void)has_insufficient_material(WHITE, pos);
  (void)has_insufficient_material(BLACK, pos);
  (void)checked(pos);
  MoveList<LEGAL> moves(pos);
  for (const auto& m : moves) {
    (void)pos.gives_check(m);
    StateInfo st;
    pos.do_move(m, st);
    (void)pos.is_immediate_game_end(r);
    pos.undo_move(m);
  }
  if (eval && !pos.checkers())
    (void)Eval::evaluate(pos);
}

}  // namespace fsfuzz
