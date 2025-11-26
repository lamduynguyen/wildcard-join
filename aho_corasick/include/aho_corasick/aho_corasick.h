#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

#include "art/tree.h"

namespace aho_corasick {

using PatternIndexType = std::pair<uint32_t, uint8_t>;
using EmitType         = std::vector<PatternIndexType>;

class AhoCorasick {
 public:
  AhoCorasick();
  ~AhoCorasick();

  void Insert(uint8_t *keyword_data, uint64_t keyword_size, PatternIndexType keyword_aux_index);
  auto Contain(uint8_t *keyword_data, uint64_t keyword_size) -> bool;
  auto ParseText(std::string_view text) -> std::vector<EmitType>;

 private:
  std::unique_ptr<ART_OLC::Tree> trie_;
};

}  // namespace aho_corasick
