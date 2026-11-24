// NNUE network file (UCI `setoption name EvalFile value <file>` -> Eval::NNUE::init ->
// Eval::NNUE::load_eval, src/nnue/evaluate_nnue.cpp): header (version, hash, description),
// feature-transformer and layer-stack parameters (LEB128/little-endian readers). Nets are
// downloaded from third parties. When the input loads as a net, the start position is evaluated
// with it once, as the engine does after a successful load.
//
// Input: byte 0 picks the built-in variant the net is for (UCI_Variant; NNUE::init loads a net
// for the selected variant, and the feature-transformer size depends on it,
// half_ka_v2_variants.h), the rest is the net file. Small-board / kingless variants have nets
// small enough to ship complete as seeds, so the evaluation half is reached from the start.
//
// State: load_eval re-allocates and zeroes every network buffer (NNUE::initialize) before reading,
// so no parameters carry over between inputs; UCI_Variant is set on every input, and the "net in
// use" switches the engine keeps (Eval::useNNUE, currentNnueVariant) are restored to the startup
// values after each input. The build has no embedded net (NNUE_EMBEDDING_OFF), so the startup
// state is "no net in use".
#include "fs_common.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) { fsfuzz::init_engine(); return 0; }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fsfuzz::init_engine();
  if (size < 1) return -1;
  const bool useNNUE0 = Eval::useNNUE;
  const Variant* nnueVariant0 = currentNnueVariant;

  const auto& keys = fsfuzz::builtin_variants();
  const std::string& name = keys[data[0] % keys.size()];
  Options["UCI_Variant"] = name;   // as `setoption name UCI_Variant` does (on_variant_change)
  const Variant* v = variants.find(name)->second;
  currentNnueVariant = v;          // what NNUE::init selects before load_eval
  std::istringstream is(std::string(reinterpret_cast<const char*>(data + 1), size - 1));
  if (Eval::NNUE::load_eval("fuzz.nnue", is)) {
    Eval::useNNUE = true;
    StateListPtr states(new std::deque<StateInfo>(1));
    Position pos;
    pos.set(v, v->startFen, false, &states->back(), Threads.main());
    (void)Eval::evaluate(pos);
  }

  Eval::useNNUE = useNNUE0;
  currentNnueVariant = nnueVariant0;
  return 0;
}
