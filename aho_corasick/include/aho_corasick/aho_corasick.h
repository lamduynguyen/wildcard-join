#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <unordered_set>
#include <vector>

#include "gtest/gtest_prod.h"
#include "tbb/concurrent_vector.h"
#include "tbb/enumerable_thread_specific.h"

#include "art/epoche.h"
#include "art/tree.h"
#include "common/typedef.h"
#include "common/util.h"

namespace aho_corasick {

/**
 * @brief We support scenario where one pattern may have more than one keywords
 * E.g., SQL condition `WHERE R.s LIKE '%' || 'Hello' || '%' || 'Welcome'`
 * In such scenarios, those keywords are uniquely identified using:
 * - Pattern ID, i.e., Row ID of the relation `Pattern`
 * - (Matched) Offset within pattern
 * - (Matched) Offset within text
 *
 * Note that, a keyword may also appear multiple times across multiple patterns.
 */
struct PatternIndexType {
  u64 pattern_id : 56;
  u8 offset_within_pt;

  PatternIndexType(u64 pattern_id, uint8_t offset_within_pt)
      : pattern_id(pattern_id), offset_within_pt(offset_within_pt) {}
};

struct MatchingOutputType {
  PatternIndexType pattern_index;
  u64 offset_within_text;

  MatchingOutputType(PatternIndexType &pattern_index, u64 offset_within_text)
      : pattern_index(pattern_index), offset_within_text(offset_within_text) {}

  bool operator==(const MatchingOutputType &other) const {
    return pattern_index.pattern_id == other.pattern_index.pattern_id &&
           pattern_index.offset_within_pt == other.pattern_index.offset_within_pt &&
           offset_within_text == other.offset_within_text;
  }

  struct Hasher {
    std::size_t operator()(const MatchingOutputType &k) const noexcept {
      auto combined = (static_cast<uint64_t>(k.pattern_index.pattern_id) << 8) | k.pattern_index.offset_within_pt;
      u64 h1        = HashFn(combined);
      u64 h2        = HashFn(k.offset_within_text);
      return h1 ^ (h2 + 0x9e3779b97f4a7c15 + (h1 << 12) + (h1 >> 4));  // adopted from boost::hash_combine
    }
  };
};

static_assert(sizeof(PatternIndexType) == 8);
static_assert(sizeof(MatchingOutputType) == 16);

using OutputEmitType = std::unordered_set<MatchingOutputType, MatchingOutputType::Hasher>;

class AhoCorasick {
 public:
  AhoCorasick();
  ~AhoCorasick() = default;

  auto Local() -> ART::ThreadInfo;
  void Insert(const char *keyword_data, uint64_t keyword_size, PatternIndexType keyword_aux_index, ART::ThreadInfo &t);
  void BuildSuffixLink(u16 number_of_threads);
  auto ParseText(std::string_view text) -> OutputEmitType;

 private:
  FRIEND_TEST(TestAhoCorasick, SuffixLink);
  auto GetRoot() -> ART::N *;

  std::unique_ptr<ART::Tree> trie_;
  // A pair of keyword (in std::string format) and vector of its indexing info (referring back to the relation Pattern)
  tbb::concurrent_vector<std::vector<PatternIndexType>> pattern_;
};

}  // namespace aho_corasick
