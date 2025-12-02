#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <unordered_set>
#include <vector>

#include "art/epoche.h"
#include "art/tree.h"
#include "tbb/concurrent_vector.h"
#include "tbb/enumerable_thread_specific.h"

namespace aho_corasick {

/**
 * @brief We support scenario where one pattern may have more than one keywords
 * E.g., SQL condition `WHERE R.s LIKE '%' || 'Hello' || '%' || 'Welcome'`
 * In such scenarios, those keywords are uniquely identified using:
 * - Pattern ID, i.e., Row ID of the relation `Pattern`
 * - Offset within pattern
 *
 * Note that, a keyword may also appear multiple times across multiple patterns.
 */
struct PatternIndexType {
  uint64_t pattern_id : 48;
  uint8_t offset_within_pt;

  // TODO: Should we store offset within text?

  PatternIndexType(uint32_t pattern_id, uint8_t offset_within_pt)
      : pattern_id(pattern_id), offset_within_pt(offset_within_pt) {}

  ~PatternIndexType() = default;

  bool operator==(const PatternIndexType &other) const {
    return pattern_id == other.pattern_id && offset_within_pt == other.offset_within_pt;
  }

  struct Hasher {
    std::size_t operator()(const PatternIndexType &k) const noexcept {
      uint64_t combined = (static_cast<uint64_t>(k.pattern_id) << 8) | k.offset_within_pt;
      return std::hash<uint64_t>()(combined);
    }
  };
};

static_assert(sizeof(PatternIndexType) == 8);

using KeywordType    = std::string;
using OutputEmitType = std::unordered_set<PatternIndexType, PatternIndexType::Hasher>;

class AhoCorasick {
 public:
  AhoCorasick();
  ~AhoCorasick() = default;

  auto Local() -> ART::ThreadInfo;
  void Insert(const char *keyword_data, uint64_t keyword_size, PatternIndexType keyword_aux_index, ART::ThreadInfo &t);
  void BuildSuffixLink(uint32_t until_level = std::numeric_limits<uint32_t>::max());
  auto ParseText(std::string_view text) -> OutputEmitType;

 private:
  std::unique_ptr<ART::Tree> trie_;
  // A pair of keyword (in std::string format) and vector of its indexing info (referring to the relation Pattern)
  tbb::concurrent_vector<std::pair<KeywordType, std::vector<PatternIndexType>>> pattern_;

  // TODO: Implement path compression after building the trie
};

}  // namespace aho_corasick
