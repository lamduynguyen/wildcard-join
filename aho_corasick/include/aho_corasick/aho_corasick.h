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

struct EmitType {
  std::vector<PatternIndexType> emit;
  std::mutex latch;

  inline auto Append(PatternIndexType index) {
    std::lock_guard<std::mutex> lock(latch);
    emit.emplace_back(index);
  }
};

class AhoCorasick {
 public:
  AhoCorasick(uint64_t no_patterns);
  ~AhoCorasick() = default;

  auto Local() -> ART::ThreadInfo;
  void Insert(char *keyword_data, uint64_t keyword_size, PatternIndexType keyword_aux_index, ART::ThreadInfo &t);
  void BuildSuffixLink();
  auto Contain(char *keyword_data, uint64_t keyword_size) -> bool;
  auto ParseText(std::string_view text) -> std::vector<EmitType>;

 private:
  std::unique_ptr<ART::Tree> trie_;
  std::vector<std::string> pattern_;
  std::vector<EmitType> pattern_indices_;
};

}  // namespace aho_corasick
