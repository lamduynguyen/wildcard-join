#pragma once

#include "aho_corasick/aho_corasick.h"
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

struct Tokenizer {
  static constexpr u32 WRONG_OFFSET = -1U;
  static constexpr auto PERCENTAGE  = '%';
  static constexpr auto UNDERSCORE  = '_';

  struct TextUnit {
    u32 start;
    u32 len;
  };

  static auto NextLiteral(const char *s, size_t slen, u32 &pos, u32 &max_underscore_cnt) -> TextUnit;
  static auto NextSegment(const char *s, size_t slen, u32 &pos) -> TextUnit;
};

/**
 * @brief A queue for managing delayed literal matches that cannot be applied immediately.
 *        This is necessary for Unicode text, because the byte offset of subsequent literals
 *        cannot always be determined in advance.
 *
 * Example:
 *   Text: "😀🐍🍕" (each emoji is multiple bytes in UTF-8)
 *   Pattern: "😀_🍕", where "_" is a wildcard matching a single code point.
 *
 *   We use the notation [text_offset, pattern_offset] to indicate matches.
 *
 *   1. The first literal "😀" matches at text offset 0 => [0, 0].
 *   2. The wildcard "_" matches the next emoji "🐍", starting at text offset 4 => [4, 4].
 *   3. The final literal "🍕" is expected after the wildcard.
 *      One might expect its match to be [8, 8], based on previous matches, but this is incorrect.
 *      Because the wildcard "_" represents a single code point (1 pattern byte), the correct match is actually [8, 6].
 *
 * In such cases, we cannot immediately determine the correct pattern-to-text byte mapping for the next literal.
 * The DelayedMatchQueue allows these matches to be scheduled and applied later,
 *    once the parser reaches the corresponding code point index.
 * This ensures correct mapping even for multi-byte Unicode characters.
 */
class DelayedMatchQueue {
 public:
  DelayedMatchQueue() = default;

  // @brief Schedule a delayed literal match to be processed later.
  //
  // When a literal cannot be immediately mapped to the correct text byte offset
  // (e.g., due to Unicode multi-byte characters or wildcard placeholders),
  // this function enqueues the match for later processing.
  //
  // @param pattern_id       ID of the pattern this literal belongs to.
  // @param next_cp_index    Code point index in the text when this match becomes ready.
  // @param next_patt_offset Start offset of the next literal in the pattern.
  // @param prev_patt_offset Start offset of the previous literal in the pattern.
  //
  // @note `ready_cp_idx` is calculated externally and should correspond to the text
  //       position after all skipped code points (underscores) for this literal.
  //       The queue will process matches once the parser reaches this code point index.
  //       This ensures that multi-byte Unicode characters are correctly handled on byte granularity.
  inline void Schedule(u32 pattern_id, u64 ready_cp_idx, u32 next_patt_offset, u32 prev_patt_offset) {
    fmt::println("Delay matching: [when_process(based on codepoint index): {}, next_pat_start_pos: {}]", ready_cp_idx,
                 next_patt_offset);
    queue_.emplace(DelayedMatch{.pattern_id       = pattern_id,
                                .next_pattern_pos = next_patt_offset,
                                .prev_pattern_pos = prev_patt_offset,
                                .ready_cp_idx     = ready_cp_idx});
  }

  // @brief Remove the next match from the queue
  inline void Pop() { queue_.pop(); }

  // @brief Returns a reference to the next match in the queue
  inline const auto &Front() const { return queue_.top(); }

  // @brief Returns true if the queue is empty
  inline auto Empty() const { return queue_.empty(); }

  // @brief Returns true if the front match is ready to be processed
  //        based on the current code point index
  inline auto FrontReady(u64 current_cp_idx) const {
    return !queue_.empty() && queue_.top().ready_cp_idx <= current_cp_idx;
  }

 private:
  // @brief Represents a delayed match scheduled for future processing.
  struct DelayedMatch {
    u32 pattern_id;        // ID of the pattern this match belongs to
    u32 next_pattern_pos;  // Next literal start position in the pattern
    u32 prev_pattern_pos;  // Previous literal start position in the pattern
    u64 ready_cp_idx;      // Code point index when this match becomes ready

    // Comparison operator for priority queue (min-heap)
    bool operator>(const DelayedMatch &other) const { return ready_cp_idx > other.ready_cp_idx; }
  };

  // Min-heap priority queue based on ready_cp_idx
  std::priority_queue<DelayedMatch, std::vector<DelayedMatch>, std::greater<DelayedMatch>> queue_;
};

/**
 * @brief Matcher implements the matching logic, i.e., whether the text matches the wildcard pattern.
 * One `Matcher` per `Skeleton`.
 */
class Matcher {
 public:
  Matcher(u64 max_size) : match_(max_size) {}

  inline auto CurrentSegmentIdx() { return segment_idx_; }

  inline auto GetAndRemove(u64 text_pat_diff) { return match_.GetAndRemove(text_pat_diff); }

  inline auto Contain(u64 text_pat_diff) const { return match_.Contain(text_pat_diff); }

  inline auto Upsert(u64 text_pat_diff, u64 pat_cursor) { match_.Upsert(text_pat_diff, pat_cursor); }

 private:
  friend class Skeleton;

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
  inline auto OnlyWildcard() { return only_wildcard_; }

  inline auto operator[](int idx) -> Segment & { return seg_[idx]; }

  inline auto IsEmpty() -> bool { return seg_.empty(); }

  inline auto Size() { return seg_.size(); }

  inline auto Last() -> Segment & { return seg_.back(); }

  static auto SpecialMatchEmptyPattern(const char *s, size_t slen, const char *p, size_t plen) -> bool;

  /* Matching utilities */
  auto InitializeMatcher() -> Matcher;
  auto TryMatchingLiteral(const MatchingOutputType &ac_match, Matcher &matcher, u64 curr_cp_index,
                          DelayedMatchQueue &delay_queue) -> bool;
  auto ValidLastLiteral(const MatchingOutputType &ac_match, const Matcher &matcher, const char *text, u64 text_length)
    -> bool;
  auto AdvanceNextSegment(u64 min_text_next_start_pos, Matcher &matcher) -> bool;

 private:
  auto MayMatch(const MatchingOutputType &ac_match, Matcher &matcher) -> bool;

  bool only_wildcard_;
  std::vector<Segment> seg_;
};

}  // namespace aho_corasick