#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

#include "art/epoche.h"
#include "art/tree.h"
#include "tbb/enumerable_thread_specific.h"

namespace aho_corasick {

using PatternIndexType = std::pair<uint32_t, uint8_t>;
using EmitType         = std::vector<PatternIndexType>;

struct ThreadLocalInfo {
  std::vector<std::string> &pattern;
  std::vector<EmitType> &pattern_indices;
  ART_OLC::ThreadInfo &art_tlocal;

  ThreadLocalInfo(std::vector<std::string> &pattern, std::vector<EmitType> &pattern_indices,
                  ART_OLC::ThreadInfo art_tlocal)
      : pattern(pattern), pattern_indices(pattern_indices), art_tlocal(art_tlocal) {}

  ~ThreadLocalInfo() = default;

  void LoadKey(TupleID tid, Key &key);
  auto CheckKey(const TupleID tid, const Key &k) -> bool;
};

class AhoCorasick {
 public:
  AhoCorasick();
  ~AhoCorasick() = default;

  auto Local() -> ThreadLocalInfo;
  void Insert(char *keyword_data, uint64_t keyword_size, PatternIndexType keyword_aux_index, ThreadLocalInfo &t);
  auto Contain(char *keyword_data, uint64_t keyword_size) -> bool;
  auto ParseText(std::string_view text) -> std::vector<EmitType>;

 private:
  std::unique_ptr<ART_OLC::Tree> trie_;
  tbb::enumerable_thread_specific<std::vector<std::string>> pattern_;
  tbb::enumerable_thread_specific<std::vector<EmitType>> pattern_indices_;
};

}  // namespace aho_corasick
