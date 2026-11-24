// Variant configuration (variants.ini syntax): UCI `load <<EOF` / `check <<EOF` / VariantPath
// -> VariantMap::parse_istream -> VariantParser, Variant::conclude, betza piece parsing; then every
// variant the input defines is set up from its start FEN and searched one ply + evaluated.
// Input: byte 0 bit0 selects the DoCheck (`check`) parser; the rest is the ini text.
#include "fs_common.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) { fsfuzz::init_engine(); return 0; }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fsfuzz::init_engine();
  if (size < 1) return -1;
  std::stringstream ss(std::string(reinterpret_cast<const char*>(data + 1), size - 1));
  if (data[0] & 1) {
    variants.parse_istream<true>(ss);   // check mode removes what it added itself
    return 0;
  }
  variants.parse_istream<false>(ss);

  std::set<std::string> builtin(fsfuzz::builtin_variants().begin(), fsfuzz::builtin_variants().end());
  for (auto const& e : variants) {
    if (builtin.count(e.first)) continue;
    const Variant* v = e.second;
    UCI::init_variant(v);
    PSQT::init(v);
    // Position::set requires a correct FEN (src/position.cpp: "make sure that input FENs are
    // correct, this is assumed to be the responsibility of the GUI"): set up only variants whose
    // start FEN FEN::validate_fen accepts for that variant.
    if (FEN::validate_fen(v->startFen, v, v->chess960) != FEN::FEN_OK) continue;
    StateListPtr states(new std::deque<StateInfo>(1));
    Position pos;
    pos.set(v, v->startFen, v->chess960, &states->back(), Threads.main());
    fsfuzz::exercise_position(pos, true);
  }
  fsfuzz::drop_added_variants();
  const Variant* def = variants.find(Options["UCI_Variant"])->second;
  UCI::init_variant(def);
  PSQT::init(def);
  return 0;
}
