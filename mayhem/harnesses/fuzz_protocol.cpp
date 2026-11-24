// Engine protocol front end: UCI::loop reading a command stream from std::cin (uci.cpp), as a GUI
// drives the engine. Built twice:
//   fuzz_uci     UCI/USI/UCCI dialects: position/go/setoption/banmoves/load <<EOF/check <<EOF/...
//   fuzz_xboard  (-DFS_XBOARD) the XBoard/CECP state machine (xboard.cpp) after `xboard`.
//
// Commands are allow-listed after tokenizing each line the way uci.cpp does (istringstream >>):
//   * searches are bounded (`go` keeps depth/mate/searchmoves and always gets `nodes 2000`;
//     XBoard search depth is fixed with `sd 2`; time controls, infinite, ponder, analyze dropped);
//   * file/process-touching commands and options are dropped (bench, export_net, load/check <path>,
//     VariantPath, EvalFile, SyzygyPath, Debug Log File, Threads, Hash, NNUE, Ponder);
//   * the next command is only handed to the engine once the previous search finished (the cin
//     streambuf waits), so every input replays deterministically;
//   * board-setting commands reach Position::set only with a FEN that FEN::validate_fen accepts
//     (FEN_OK) for the variant/chess960 setting in force when the line is served: `position fen`,
//     `position startpos` (a variant loaded with `load <<EOF` can carry any startFen) and XBoard
//     `setboard`. Position::set's contract (src/position.cpp: "make sure that input FENs are
//     correct, this is assumed to be the responsibility of the GUI") makes validation the GUI's
//     job, as in the fen/san harnesses. `position sfen` is dropped: validate_fen cannot check SFEN.
// State kept across inputs is reset after each run: added variants are removed, every option is
// restored to its pre-run value, the protocol goes back to UCI, the last search's state list is
// dropped and the hash/history is cleared.
#include "fs_common.h"
#include "xboard.h"

namespace {

std::string lower(std::string s) {
  for (auto& c : s) c = char(std::tolower((unsigned char)c));
  return s;
}

// Option names an input may not set (lower-case substrings).
bool option_blocked(const std::string& name) {
  static const char* bad[] = {"file", "path", "threads", "hash", "nnue", "log", "ponder", "analysemode"};
  std::string l = lower(name);
  for (const char* b : bad)
    if (l.find(b) != std::string::npos) return true;
  return false;
}

const Variant* option_variant() {
  auto it = variants.find(std::string(Options["UCI_Variant"]));
  return it == variants.end() ? nullptr : it->second;
}

bool fen_ok(const std::string& fen, const Variant* v, bool chess960) {
  return v && fen.find('\0') == std::string::npos && FEN::validate_fen(fen, v, chess960) == FEN::FEN_OK;
}

// Position::set precondition for one served line (see the header); false drops the line.
// `boardVariant`/`board960` are what the XBoard state machine's board was last set up with
// (its `setboard` pass check re-parses the FEN with pos.variant()).
// Lines of a `load|check <<EOF` body are variants.ini text, not commands (filter() marks them).
std::vector<bool> raw_line;

bool board_line_ok(const std::string& line, const Variant* boardVariant, bool board960) {
  std::istringstream is(line);
  std::string tok;
  if (!(is >> tok)) return true;
  const Variant* v = option_variant();
  const bool c960 = bool(Options["UCI_Chess960"]);
#ifdef FS_XBOARD
  if (tok != "setboard") return true;
  std::string fen;
  std::getline(is >> std::ws, fen);   // as xboard.cpp reads it
  if (fen.empty()) return true;       // the variant's start position
  return fen_ok(fen, v, c960) && fen_ok(fen, boardVariant, board960);
#else
  (void)boardVariant; (void)board960;
  if (tok != "position") return true;
  if (!(is >> tok)) return true;
  if (tok == "sfen") return false;
  if (tok == "startpos") return v && fen_ok(v->startFen, v, c960);
  if (tok != "fen") return true;      // uci.cpp ignores the line
  std::string fen;
  while (is >> tok && tok != "moves") fen += (fen.empty() ? "" : " ") + tok;
  return fen_ok(fen, v, c960);
#endif
}

// Feeds one line at a time; before every line it waits for a running search to finish, so the
// options the line is checked against are the ones the engine will use for it.
struct SerialBuf : std::streambuf {
  std::vector<std::string> lines;
  size_t idx = 0;
  std::string cur;
  const Variant* boardVariant = nullptr;
  bool board960 = false;
  bool resync = true;
  int underflow() override {
    if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
    Threads.main()->wait_for_search_finished();
    if (resync) {   // the previous line (re)set the XBoard board from the options
      boardVariant = option_variant();
      board960 = bool(Options["UCI_Chess960"]);
      resync = false;
    }
    while (idx < lines.size() && !(idx < raw_line.size() && raw_line[idx])
           && !board_line_ok(lines[idx], boardVariant, board960)) idx++;
    if (idx >= lines.size()) return traits_type::eof();
    std::istringstream is(lines[idx]);
    std::string tok;
    is >> tok;
    resync = tok == "new" || tok == "variant" || tok == "setboard" || tok == "holding";
    cur = lines[idx++] + "\n";
    setg(&cur[0], &cur[0], &cur[0] + cur.size());
    return traits_type::to_int_type(*gptr());
  }
};

std::map<std::string, std::string> saved_options;

void snapshot_options() {
  for (auto& e : Options)
    if (e.second.get_type() != "button") saved_options[e.first] = std::string(e.second);
}

void restore_state() {
  Threads.main()->wait_for_search_finished();
  CurrentProtocol = UCI_GENERAL;
  Threads.setupStates.reset();   // the last search's position history belongs to this input's loop
  for (auto& e : saved_options)
    if (std::string(Options[e.first]) != e.second) Options[e.first] = e.second;
  fsfuzz::drop_added_variants();
  Options["UCI_Variant"].set_combo(variants.get_keys());
  Search::clear();
}

#ifdef FS_XBOARD
bool move_shaped(const std::string& t) {
  if (t.size() < 2 || t.size() > 12) return false;
  for (char c : t)
    if (!std::isalnum((unsigned char)c) && !std::strchr("@*+=-,'", c)) return false;
  return true;
}

std::vector<std::string> filter(const std::string& in) {
  std::vector<std::string> out = {"xboard", "protover 2", "sd 2"};
  static const std::set<std::string> ok = {
      "protover", "accepted", "rejected", "hover", "put", "lift", "ping", "new", "variant", "force",
      "result", "?", "go", "setboard", "option", "exit", "undo", "remove", "partner", "ptell",
      "holding", "perft", "d", "eval", "usermove", "easy", "computer", "post", "nopost", "random",
      "draw", "name", "rating", "ics", "playother", "white", "black", "edit", "bk", "hint"};
  std::istringstream ls(in);
  std::string line;
  while (std::getline(ls, line)) {
    std::istringstream is(line);
    std::string tok;
    if (!(is >> tok)) continue;
    if (tok == "option") {
      std::string rest;
      is.get();
      std::getline(is, rest);
      if (option_blocked(rest.substr(0, rest.find('=')))) continue;
    } else if (tok == "perft") {   // perft 0 (or a non-number) would be an unbounded search
      int d = 0;
      is >> d;
      line = "perft " + std::to_string(std::max(1, std::min(d, 2)));
    } else if (!ok.count(tok) && !move_shaped(tok)) continue;
    out.push_back(line);
  }
  return out;
}
#else
std::vector<std::string> filter(const std::string& in) {
  std::vector<std::string> out;
  static const std::set<std::string> ok = {
      "uci", "usi", "ucci", "ucicyclone", "isready", "ucinewgame", "usinewgame", "uccinewgame",
      "position", "go", "setoption", "banmoves", "flip", "d", "eval", "compiler", "load", "check"};
  std::istringstream ls(in);
  std::string line, heredoc;
  bool in_heredoc = false;
  while (std::getline(ls, line)) {
    if (in_heredoc) {   // variant config body of `load|check <<EOF`, read raw by uci.cpp load()
      raw_line.resize(out.size(), false);
      raw_line.push_back(true);
      out.push_back(line);
      if (line == heredoc) in_heredoc = false;
      continue;
    }
    std::istringstream is(line);
    std::string tok;
    if (!(is >> tok) || !ok.count(tok)) continue;
    if (tok == "load" || tok == "check") {
      std::string arg;
      std::getline(is >> std::ws, arg);
      if (arg.rfind("<<", 0) != 0) continue;   // a file path: not part of the fuzzed input
      if (!(std::istringstream(arg.substr(2)) >> heredoc)) heredoc = "";
      in_heredoc = true;
    } else if (tok == "setoption") {
      std::string t, name;
      is >> t;   // "name"
      while (is >> t && t != "value") name += (name.empty() ? "" : " ") + t;
      if (option_blocked(name)) continue;
    } else if (tok == "go") {
      // `nodes` comes first: uci.cpp stops reading the line at the first unparsable number.
      std::string g = "go nodes 2000", t;
      while (is >> t) {
        if (t == "depth" || t == "mate") {
          int n;
          if (is >> n) g += " " + t + " " + std::to_string(n);
          else break;
        } else if (t == "perft") {
          int d = 0;
          is >> d;
          g += " perft " + std::to_string(std::max(1, std::min(d, 2)));
          break;
        } else if (t == "searchmoves") {   // must be last on the line
          g += " searchmoves";
          while (is >> t) g += " " + t;
          break;
        }
      }
      line = g;
    }
    out.push_back(line);
  }
  return out;
}
#endif

}  // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
  fsfuzz::init_engine();
  snapshot_options();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (saved_options.empty()) LLVMFuzzerInitialize(nullptr, nullptr);
  std::string in(reinterpret_cast<const char*>(data), size);
  raw_line.clear();
  SerialBuf buf;
  buf.lines = filter(in);
  std::streambuf* old = std::cin.rdbuf(&buf);
  std::cin.clear();
  char arg0[] = "fairy-stockfish";
  char* argv[] = {arg0, nullptr};
  UCI::loop(1, argv);
  Threads.main()->wait_for_search_finished();
  std::cin.rdbuf(old);
  std::cin.clear();
  delete XBoard::stateMachine;
  XBoard::stateMachine = nullptr;
  restore_state();
  return 0;
}
