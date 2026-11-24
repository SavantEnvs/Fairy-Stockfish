// FEN parsing: the binding API's FEN::validate_fen (apiutil.h, pyffish validate_fen / ffish
// validateFen) on every input, then Position::set + move generation/eval on the FENs it accepts.
// Position::set's contract (src/position.cpp: "not very robust - make sure that input FENs are
// correct, this is assumed to be the responsibility of the GUI") makes validation the caller's
// job, so only FEN_OK strings reach it; a defect past that gate is a FEN upstream accepts.
// Input: byte 0 picks a built-in variant, byte 1 flags (bit0 chess960), rest is the FEN.
#include "fs_common.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) { fsfuzz::init_engine(); return 0; }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fsfuzz::init_engine();
  if (size < 2) return -1;
  const auto& keys = fsfuzz::builtin_variants();
  const std::string& name = keys[data[0] % keys.size()];
  Options["UCI_Variant"] = name;   // as `setoption name UCI_Variant` does (on_variant_change)
  const Variant* v = variants.find(name)->second;
  bool chess960 = data[1] & 1;
  std::string fen(reinterpret_cast<const char*>(data + 2), size - 2);
  // UCI hands Position::set a space-joined token list; a NUL can never appear in it.
  if (fen.find('\0') != std::string::npos) return -1;

  if (FEN::validate_fen(fen, v, chess960) != FEN::FEN_OK) return 0;   // Position::set precondition

  StateListPtr states(new std::deque<StateInfo>(1));
  Position pos;
  pos.set(v, fen, chess960, &states->back(), Threads.main());
  fsfuzz::exercise_position(pos, true);
  return 0;
}
