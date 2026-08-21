#pragma once

#include "art/tree.h"
#include "common/typedef.h"
#include "common/util.h"

#include "roaring/roaring.hh"
#include "tbb/concurrent_unordered_map.h"

#include <atomic>
#include <memory>
#include <vector>

// The test build pulls in gtest, which defines FRIEND_TEST.  A library build
// must not need gtest just to parse this header, so fall back to the same
// declaration gtest would generate.
#ifndef FRIEND_TEST
#define FRIEND_TEST(test_case_name, test_name) friend class test_case_name##_##test_name##_Test
#endif

namespace aho_corasick {

struct TextParserIterator;

struct PatternIndexType {
  u32 pattern_id;
  u32 start_pos;

  PatternIndexType(u32 pattern_id, u32 offset_within_pt) : pattern_id(pattern_id), start_pos(offset_within_pt) {}

  auto ToUint() const { return (static_cast<u64>(pattern_id) << 32) | static_cast<u64>(start_pos); }

  static PatternIndexType FromUint(u64 value) {
    return PatternIndexType(static_cast<u32>(value >> 32), static_cast<u32>(value & 0xFFFFFFFFULL));
  }
};

struct MatchingOutputType {
  PatternIndexType pattern_index;
  u32 literal_len;
  u64 text_start_pos;

  MatchingOutputType(PatternIndexType &pattern_index, u32 literal_len, u64 offset_text)
      : pattern_index(pattern_index), literal_len(literal_len), text_start_pos(offset_text) {}

  bool operator==(const MatchingOutputType &other) const {
    return pattern_index.pattern_id == other.pattern_index.pattern_id &&
           pattern_index.start_pos == other.pattern_index.start_pos && text_start_pos == other.text_start_pos;
  }
};

// A vector and not a hash set. The matches emitted at one code point come from
// the terminal node plus its output link chain, which are distinct nodes, so
// they are distinct literals, and the pattern occurrence sets of two distinct
// literals are disjoint. Nothing can repeat, so the set was paying for a
// dedup that had nothing to do. TextParserIterator asserts that in debug
// builds rather than leaving it as a claim in a comment.
using OutputEmitType = std::vector<MatchingOutputType>;

// Bundles the pattern bitmap with the keyword's byte length, so Leaf no longer
// needs to store the key.  literal_len is the length excluding the null terminator.
struct LiteralEntry {
  roaring::Roaring64Map bitmap;
  u32 literal_len;

  LiteralEntry(u32 literal_len) : literal_len(literal_len) {}
};

class AhoCorasick {
 public:
  AhoCorasick();
  ~AhoCorasick() = default;

  auto GetRoot() const -> ART::N256 *;

  /**
   * @brief How many distinct literals this automaton holds.
   *
   * Distinct by byte string, so inserting the same literal for two patterns
   * counts once. Relaxed because the only caller that can race with an
   * insert is a progress report, and a build that has finished has already
   * synchronised through whatever joined its threads.
   */
  inline auto NumUniqueLiterals() const -> u64 { return unique_literal_cnt_.load(std::memory_order_relaxed); }

  static auto VisitCodePoint(ART::N256 *cur, const char *cp, u8 cp_len) -> ART::N256 *;
  void Insert(const char *keyword, uint64_t keyword_len, const PatternIndexType &keyword_aux_index);
  void BuildSuffixLink();

 private:
  friend struct TextParserIterator;

  FRIEND_TEST(TestAhoCorasick, SingleByteUnicode);
  FRIEND_TEST(TestAhoCorasick, MultiByteUnicode);
  FRIEND_TEST(TestAhoCorasick, LiteralIdsAreDensePerInstance);

  struct BFSNodeItem {
    ART::N256 *node;
    ART::N256 *last_codepoint_node;
    std::array<uint8_t, 6> bytes_since_last_cp;
    uint8_t length;
  };

  std::unique_ptr<ART::Tree> trie_;
  tbb::concurrent_unordered_map<TupleID, LiteralEntry> literal_map_;

  // Hands out the key for literal_map_, one per distinct literal. Still
  // atomic because Insert is the concurrent path the paper is about, but per
  // instance rather than per process, so two tries built at the same time no
  // longer serialise on the same cache line.
  std::atomic<u64> unique_literal_cnt_;
};

static_assert(sizeof(PatternIndexType) == 8);
static_assert(sizeof(MatchingOutputType) == 24);

}  // namespace aho_corasick
