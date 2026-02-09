#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <unordered_set>
#include <vector>

#include "gtest/gtest_prod.h"
#include "tbb/concurrent_vector.h"

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
  u64 pattern_id;
  u64 start_pos;    // Start position within pattern
  u64 keyword_len;  // This is auxiliary info to help with matching phase later,
                    // and no need to be used to determine uniqueness

  PatternIndexType(u64 pattern_id, u64 offset_within_pt, u64 keyword_len)
      : pattern_id(pattern_id), start_pos(offset_within_pt), keyword_len(keyword_len) {}
};

struct MatchingOutputType {
  PatternIndexType pattern_index;
  u64 text_start_pos;  // The start offset within text that this token matches

  MatchingOutputType(PatternIndexType &pattern_index, u64 offset_text)
      : pattern_index(pattern_index), text_start_pos(offset_text) {}

  // These three properties are (and must be) unique
  bool operator==(const MatchingOutputType &other) const {
    return pattern_index.pattern_id == other.pattern_index.pattern_id &&
           pattern_index.start_pos == other.pattern_index.start_pos && text_start_pos == other.text_start_pos;
  }

  struct Hasher {
    std::size_t operator()(const MatchingOutputType &k) const noexcept {
      auto combined = (static_cast<uint64_t>(k.pattern_index.pattern_id) << 8) | k.pattern_index.start_pos;
      u64 h1        = HashFn(combined);
      u64 h2        = HashFn(k.text_start_pos);
      return h1 ^ (h2 + 0x9e3779b97f4a7c15 + (h1 << 12) + (h1 >> 4));  // adopted from boost::hash_combine
    }
  };
};

static_assert(sizeof(PatternIndexType) == 24);
static_assert(sizeof(MatchingOutputType) == 32);

using OutputEmitType = std::unordered_set<MatchingOutputType, MatchingOutputType::Hasher>;

class AhoCorasick {
 public:
  AhoCorasick();
  ~AhoCorasick() = default;

  auto Local() -> ART::ThreadInfo;
  void Insert(const char *keyword, uint64_t keyword_len, const PatternIndexType &keyword_aux_index, ART::ThreadInfo &t);
  void BuildSuffixLink(u16 number_of_threads);

  // Two way to parse a text: One-round or iteratively
  auto ParseText(const char *text, size_t text_len) -> OutputEmitType;

  struct IterativeParseText {
    const char *text;
    size_t text_len;
    size_t next_offset;
    ART::N *ptr;
  };

  auto StartIterativeParseText(const char *text, size_t text_len) -> IterativeParseText;
  auto ContinueParseText(IterativeParseText &ite) -> OutputEmitType;

 private:
  FRIEND_TEST(TestAhoCorasick, SuffixLink);
  auto GetRoot() -> ART::N *;

  std::unique_ptr<ART::Tree> trie_;
  // A pair of keyword (in std::string format) and vector of its indexing info (referring back to the relation Pattern)
  tbb::concurrent_vector<std::vector<PatternIndexType>> pattern_;
};

}  // namespace aho_corasick
