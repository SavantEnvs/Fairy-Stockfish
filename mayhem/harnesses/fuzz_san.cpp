// Binding API over a game (apiutil.h, as pyffish/ffish.js use it): build a position from a FEN and a
// UCI move list (pyffish buildPosition), and at every ply render all legal moves in the chosen
// notation (SAN::move_to_san), check game-end/insufficient-material rules and the FEN.
// Input: byte 0 variant, byte 1 notation, byte 2 bit0 chess960; then "<fen or empty>\n<move> <move> ...".
#include "fs_common.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) { fsfuzz::init_engine(); return 0; }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fsfuzz::init_engine();
  if (size < 3) return -1;
  const auto& keys = fsfuzz::builtin_variants();
  const std::string& name = keys[data[0] % keys.size()];
  Options["UCI_Variant"] = name;   // as `setoption name UCI_Variant` does (on_variant_change)
  const Variant* v = variants.find(name)->second;
  Notation n = Notation(data[1] % (NOTATION_THAI_LAN + 1));
  if (n == NOTATION_DEFAULT) n = default_notation(v);
  bool chess960 = data[2] & 1;
  std::string rest(reinterpret_cast<const char*>(data + 3), size - 3);
  if (rest.find('\0') != std::string::npos) return -1;   // C strings at the binding boundary
  size_t nl = rest.find('\n');
  std::string fen = rest.substr(0, nl);
  std::string movestr = nl == std::string::npos ? "" : rest.substr(nl + 1);
  if (fen.empty() || fen == "startpos") fen = v->startFen;
  // Position::set requires a correct FEN (src/position.cpp: "make sure that input FENs are
  // correct, this is assumed to be the responsibility of the GUI"); the bindings check with
  // FEN::validate_fen, so only FEN_OK strings reach it. Moves need no gate: UCI::to_move only
  // returns legal moves.
  if (FEN::validate_fen(fen, v, chess960) != FEN::FEN_OK) return 0;

  StateListPtr states(new std::deque<StateInfo>(1));
  Position pos;
  pos.set(v, fen, chess960, &states->back(), Threads.main());

  std::istringstream ms(movestr);
  std::string tok;
  int plies = 0;
  while (true) {
    {
      MoveList<LEGAL> moves(pos);
      for (const auto& m : moves)
        (void)SAN::move_to_san(pos, m, n);
    }
    Value r;
    (void)pos.is_immediate_game_end(r);
    (void)pos.is_optional_game_end(r);
    (void)has_insufficient_material(WHITE, pos);
    (void)has_insufficient_material(BLACK, pos);
    (void)checked(pos);
    (void)pos.fen(false, true);
    if (!(ms >> tok) || ++plies > 64) break;
    Move m = UCI::to_move(pos, tok);
    if (m == MOVE_NONE) break;
    (void)SAN::move_to_san(pos, m, n);
    states->emplace_back();
    pos.do_move(m, states->back());
  }
  return 0;
}
