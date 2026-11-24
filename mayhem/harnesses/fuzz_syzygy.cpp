// Syzygy tablebase files (SyzygyPath option -> syzygy/tbprobe.cpp): input byte 0 picks the table
// kind (even: WDL .rtbw, odd: DTZ .rtbz), the rest is installed as that table for KQvK and KRvK in a
// fresh scratch directory (the other kind is absent, which the engine tolerates), the engine's
// SyzygyPath is pointed at it (Tablebases::init), and a handful of matching positions are probed
// (probe_wdl / probe_dtz / root_probe: TBFile mmap, header parsing, set_sizes/set_dtz_map,
// Huffman decompress_pairs).
//
// A file-input AFL target (Mayhemfile `afl: true`), not libFuzzer: tbprobe.cpp exit()s on a
// corrupt table (TBFile::map, size % 64 != 16), as the engine does on a bad SyzygyPath file.
// The engine init runs once before the deferred AFL forkserver; every input runs in a fresh fork.
// Nothing here starts a search, so the (parent-only) search thread is never needed in the child.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

#include "fs_common.h"
#include "syzygy/tbprobe.h"

#ifndef __AFL_INIT
#define __AFL_INIT() do {} while (0)
#endif

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: %s <tablebase file>\n", argv[0]); return 2; }
  fsfuzz::init_engine();
  __AFL_INIT();

  std::ifstream in(argv[1], std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  const char* tmp = std::getenv("TMPDIR");
  std::string dir = std::string(tmp && *tmp ? tmp : "/tmp") + "/fsyzygy.XXXXXX";
  if (!mkdtemp(&dir[0])) { std::perror("mkdtemp"); return 2; }
  if (data.empty()) return 0;
  bool dtz = data[0] & 1;
  data.erase(0, 1);
  const char* names[] = {dtz ? "KQvK.rtbz" : "KQvK.rtbw", dtz ? "KRvK.rtbz" : "KRvK.rtbw"};
  for (const char* n : names) {
    std::ofstream out(dir + "/" + n, std::ios::binary);
    out << data;
  }

  Options["SyzygyPath"] = dir;   // -> Tablebases::init(dir), as `setoption name SyzygyPath` does
  const char* fens[] = {
      "8/8/8/8/8/8/8/KQ5k w - - 0 1",   "8/8/8/4k3/8/8/8/KQ6 b - - 0 1",
      "k7/8/1Q6/8/8/8/8/7K w - - 0 1",  "8/8/8/8/8/8/8/KR5k w - - 0 1",
      "8/8/3k4/8/8/8/6R1/7K b - - 0 1", "7k/8/8/8/8/8/8/R6K w - - 0 1"};
  for (const char* f : fens) {
    StateListPtr states(new std::deque<StateInfo>(1));
    Position pos;
    pos.set(variants.find("chess")->second, f, false, &states->back(), Threads.main());
    Tablebases::ProbeState st;
    (void)Tablebases::probe_wdl(pos, &st);
    (void)Tablebases::probe_dtz(pos, &st);
    Search::RootMoves rm;
    for (const auto& m : MoveList<LEGAL>(pos)) rm.emplace_back(m);
    Tablebases::rank_root_moves(pos, rm);
  }

  for (const char* n : names) unlink((dir + "/" + n).c_str());
  rmdir(dir.c_str());
  // No Threads.set(0): under the forkserver the search thread lives only in the parent process.
  return 0;
}
