#pragma once

#include "aho_corasick/aho_corasick.h"
#include "common/flat_map.h"
#include "common/typedef.h"
#include "common/util.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace aho_corasick {

static constexpr auto PERCENTAGE = '%';
static constexpr auto UNDERSCORE = '_';

struct Token {
  u64 start;  // offset in the original string
  u64 len;

  static inline auto IsDelim(char c) { return c == UNDERSCORE || c == PERCENTAGE; };

  static auto NextToken(const char *s, size_t slen, std::size_t &pos) -> Token;
  static auto NextSegment(const char *s, size_t slen, std::size_t &pos) -> Token;
};

// Per-pattern Skeleton
class Skeleton {
 public:
  using pat_off_t                                  = u16;
  static constexpr auto INVALID_PATTERN_OFFSET     = std::numeric_limits<pat_off_t>::max();
  static constexpr auto PARTIAL_MATCH_GC_THRESHOLD = 16;

  /**
   * @brief Matcher implements the matching logic, i.e., whether the text matches the wildcard pattern.
   */
  class Matcher {
   public:
    inline auto AdvanceNextSegment(u64 min_text_next_start_pos) {
      segment_idx_++;
      min_text_start_pos_ = min_text_next_start_pos;
      match_.clear();
    }

    inline auto CurrentSegmentIdx() { return segment_idx_; }

    inline auto Contain(u64 text_pat_diff) const { return match_.contains(text_pat_diff); }

    inline auto Insert(u64 text_pat_diff, u64 pat_cursor) { match_.emplace(text_pat_diff, pat_cursor); }

    inline auto operator[](u64 text_pat_diff) -> pat_off_t & { return match_[text_pat_diff]; }

    inline auto NumberOfPartialMatches() { return match_.size(); }

   private:
    friend class Skeleton;

    u64 segment_idx_        = 0;  // The idx of the skeleton segment
    u64 min_text_start_pos_ = 0;  // The min starting offset in text that we can continue matching for seg[segment_idx]
    ankerl::unordered_dense::map<u64, pat_off_t> match_;  // A mapping of (text_cur - pat_cur) => pat_cur
  };

  /**
   * @brief Segment implements Skeleton structure.
   * That is, we compile the wildcard pattern into a set of segments -- split the wildcard by `%`.
   * The segment then stores:
   * - The set of literals -- which is separate by `_`
   * - The positional constraints between these literals,
   *    determined by the number of `_` sit between two consecutive literals
   * - Some additional properties to handle corner cases, e.g., whether the segment contains a prefix percent or not
   *
   * The literal offset is implemented using:
   * - A sorted vector of offsets
   * - A hash map of offset -> position within the above sorted vector
   * This impl allows optimal finding preceeding & next neighbors within the Segment
   */
  struct Segment {
    std::vector<pat_off_t> literal_offset;
    ankerl::unordered_dense::map<pat_off_t, pat_off_t> lit_off_p;  // pos of an `offset` value within the above vector
    u64 suffix_underscore_cnt;
    bool has_prefix_percent;
    bool has_suffix_percent;

    void Insert(pat_off_t start_pos);
    auto Contain(pat_off_t start_pos) const -> bool;
    auto IsFirstLiteral(pat_off_t start_pos) const -> bool;
    auto IsLastLiteral(pat_off_t start_pos) const -> bool;
    auto IsPreviousLiteral(pat_off_t prev_start_pos, pat_off_t start_pos) const -> bool;
    auto GetNextLiteralOffset(pat_off_t start_pos) -> pat_off_t;
  };

  Skeleton(const char *p, size_t plen, const std::function<void(Token &)> &literal_fn);
  ~Skeleton() = default;

  /* Skeleton segment (i.e., structure) utilities */
  inline auto OnlyWildcard() { return only_wildcard_; }

  inline auto operator[](int idx) -> Segment & { return seg_[idx]; }

  inline auto IsEmpty() -> bool { return seg_.empty(); }

  inline auto Size() { return seg_.size(); }

  inline auto Last() -> Segment & { return seg_.back(); }

  static auto SpecialMatchEmptyPattern(size_t slen, const char *p, size_t plen) -> bool;

  /* Matching utilities */
  auto MayMatch(const MatchingOutputType &ac_match, Matcher &matcher) -> bool;
  auto TryMatching(const MatchingOutputType &ac_match, Matcher &matcher) -> bool;
  auto SatisfyMatcher(const MatchingOutputType &ac_match, const Matcher &matcher, u64 text_length) -> bool;
  void TryGarbageCollection(u64 current_text_offset, Matcher &matcher);

 private:
  bool only_wildcard_;
  std::vector<Segment> seg_;
};

}  // namespace aho_corasick