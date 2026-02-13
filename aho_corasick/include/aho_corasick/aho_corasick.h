#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <unordered_set>
#include <vector>

#include "gtest/gtest_prod.h"
#include "roaring/roaring.hh"
#include "tbb/concurrent_unordered_map.h"

#include "art/epoche.h"
#include "art/tree.h"
#include "common/typedef.h"
#include "common/util.h"

namespace aho_corasick {

/**
 * @brief We support scenario where one pattern may have more than one keywords
 * E.g., SQL condition `WHERE R.s LIKE '%' || 'Hello' || '%' || 'Welcome'`
 * In such scenarios, those literals are uniquely identified using:
 * - Pattern ID, i.e., Row ID of the relation `Pattern`
 * - (Matched) Offset within pattern
 * - (Matched) Offset within text
 *
 * Note that, a literal may also appear multiple times across multiple patterns.
 * The first two properties are maintained in `struct PatternIndexType`, along with the literal's len.
 * The last one is stored in `MatchingOutputType`, used for Aho-Corasick matching.
 */
struct PatternIndexType {
  u32 pattern_id;
  u32 start_pos;  // Start position of this literal within pattern

  PatternIndexType(u32 pattern_id, u32 offset_within_pt) : pattern_id(pattern_id), start_pos(offset_within_pt) {}

  auto ToUint() const { return (static_cast<u64>(pattern_id) << 32) | static_cast<u64>(start_pos); }

  static PatternIndexType FromUint(u64 value) {
    u32 pattern_id = static_cast<u32>(value >> 32);
    u32 start_pos  = static_cast<u32>(value & 0xFFFFFFFFULL);
    return PatternIndexType(pattern_id, start_pos);
  }
};

struct MatchingOutputType {
  PatternIndexType pattern_index;
  u32 literal_len;     // Aux info for matching phase, no need for unique ID
  u64 text_start_pos;  // The start offset within text that this token matches

  MatchingOutputType(PatternIndexType &pattern_index, u32 literal_len, u64 offset_text)
      : pattern_index(pattern_index), literal_len(literal_len), text_start_pos(offset_text) {}

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

static_assert(sizeof(PatternIndexType) == 8);
static_assert(sizeof(MatchingOutputType) == 24);

using OutputEmitType = std::unordered_set<MatchingOutputType, MatchingOutputType::Hasher>;

class AhoCorasick {
 public:
  static std::atomic<u64> NUMBER_OF_UNIQUE_LITERALS;

  AhoCorasick();
  ~AhoCorasick() = default;

  auto Local() -> ART::ThreadInfo;
  void Insert(const char *keyword, uint64_t keyword_len, const PatternIndexType &keyword_aux_index, ART::ThreadInfo &t);
  void BuildSuffixLink(u16 number_of_threads);

  // Two way to parse a text: One-round or iteratively
  auto ParseText(const char *text, size_t text_len) -> OutputEmitType;

  struct IterativeParseText {
    const char *text;
    const size_t text_len;
    size_t text_offset;
    size_t iterator_idx;
    ART::N *ptr;

    IterativeParseText(const char *text, size_t text_len, ART::N *automaton_root)
        : text(text), text_len(text_len), text_offset(0), iterator_idx(0), ptr(automaton_root) {}

    inline auto CanAdvanceOneCodePoint() { return text_offset < text_len; }
  };

  auto GetRoot() -> ART::N *;
  auto ContinueParseText(IterativeParseText &ite) -> OutputEmitType;

 private:
  FRIEND_TEST(TestAhoCorasick, SingleByteUnicode);
  FRIEND_TEST(TestAhoCorasick, MultiByteUnicode);

  /**
   * @brief Represents an item in the BFS queue used for constructing
   *        suffix and output links in a Unicode-aware Aho-Corasick automaton.
   *
   * - `node`: The current node being processed in the BFS.
   * - `last_codepoint_node`: The closest ancestor node corresponding to the end of a complete Unicode code point.
   * - `intermediate_bytes`: The byte sequence from `last_codepoint_node` to `node`.
   *
   * Note: If `node` represents the end of a code point, then `node == last_codepoint_node`.
   */
  struct BFSNodeItem {
    ART::N *node;                                /// Current BFS node
    ART::N *last_codepoint_node;                 /// Closest ancestor node ending a code point
    std::array<uint8_t, 6> bytes_since_last_cp;  /// Bytes from last_codepoint_node to this node
    uint8_t length;  /// TODO: Only used if we want to parallelize the Suffix link construction
  };

  void AppendResult(const IterativeParseText &ite, ART::N *leaf, size_t cp_len, OutputEmitType &out_result);
  auto VisitCodePoint(ART::N *cur, const char *cp, u8 cp_len) -> ART::N *;

  std::unique_ptr<ART::Tree> trie_;
  tbb::concurrent_unordered_map<TupleID, roaring::Roaring64Map> literal_map_;
};

}  // namespace aho_corasick
