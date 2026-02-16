#pragma once

#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/delay_queue.h"
#include "aho_corasick/tokenizer.h"
#include "common/lrucache.h"
#include "common/typedef.h"
#include "common/util.h"

#include "fmt/format.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <queue>
#include <vector>

namespace aho_corasick {

class Skeleton;

/**
 * @brief Matcher implements the matching logic, i.e., whether the text matches the wildcard pattern.
 * One `Matcher` per `Skeleton`.
 */
class Matcher {
 public:
  Matcher(u64 max_size) : match_(max_size) {}

  /* Matcher utilities */
  auto TryMatchingLiteral(const Skeleton &sket, const MatchingOutputType &ac_match, u64 curr_cp_index,
                          DelayedMatchQueue &delay_queue) -> bool;
  auto AdvanceNextSegment(const Skeleton &sket, u64 min_text_next_start_pos) -> bool;

  /* Misc helpers */
  inline auto CurrentSegmentIdx() { return segment_idx_; }

  inline auto GetAndRemove(u64 text_pat_diff) { return match_.GetAndRemove(text_pat_diff); }

  inline auto Contain(u64 text_pat_diff) const { return match_.Contain(text_pat_diff); }

  inline auto Upsert(u64 text_pat_diff, u64 pat_cursor) { match_.Upsert(text_pat_diff, pat_cursor); }

 private:
  auto MayMatch(const Skeleton &sket, const MatchingOutputType &ac_match) -> bool;

  u64 segment_idx_        = 0;  // The idx of the skeleton segment
  u64 min_text_start_pos_ = 0;  // The min starting offset in text that we can continue matching for seg[segment_idx]
  LRUCache<u64, u16> match_;    // A mapping of (text_cur - pat_cur) => pat_cur
};

// Per-pattern Skeleton
class Skeleton {
 public:
  static constexpr auto INVALID_PATTERN_OFFSET = std::numeric_limits<u16>::max();

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
    std::vector<u16> literal_offset;
    ankerl::unordered_dense::map<u16, u16> lit_off_p;  // pos of an `offset` value within the above vector
    u32 suffix_underscore_cnt;
    u32 max_underscore_cnt;
    bool has_prefix_percent;
    bool has_suffix_percent;

    void Insert(u16 start_pos);
    auto Contain(u16 start_pos) const -> bool;
    auto IsFirstLiteral(u16 start_pos) const -> bool;
    auto IsLastLiteral(u16 start_pos) const -> bool;
    auto IsPreviousLiteral(u16 prev_start_pos, u16 start_pos) const -> bool;
    auto GetNextLiteralOffset(u16 start_pos) const -> u16;
  };

  Skeleton(const char *p, size_t plen, const std::function<void(Tokenizer::TextUnit &)> &literal_fn);
  ~Skeleton() = default;

  /* Skeleton segment (i.e., structure) utilities */
  inline auto OnlyWildcard() const { return only_wildcard_; }

  inline auto operator[](u64 idx) const -> const Segment & { return seg_[idx]; }

  inline auto IsEmpty() const -> bool { return seg_.empty(); }

  inline auto Size() const { return seg_.size(); }

  inline auto Last() -> Segment & { return seg_.back(); }

  static auto SpecialMatchEmptyPattern(const char *s, size_t slen, const char *p, size_t plen) -> bool;

  /* Skeleton utilities */
  auto InitializeMatcher() const -> Matcher;
  auto ValidLastLiteral(const MatchingOutputType &ac_match, u64 curr_segment_idx, const char *text,
                        const u64 text_length) const -> bool;

 private:
  friend class Matcher;

  bool only_wildcard_;
  std::vector<Segment> seg_;
};

}  // namespace aho_corasick