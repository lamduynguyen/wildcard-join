#pragma once

#include "common/typedef.h"

#include <queue>

namespace aho_corasick {

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

  // @brief Drop everything still scheduled, for reuse on the next text.
  //
  // Popping in a loop rather than assigning an empty queue, because
  // std::priority_queue keeps its container protected and assignment would
  // release the heap array. Anything left here is a literal whose successor
  // never arrived before the text ended, so the loop runs a handful of times.
  inline void Clear() {
    while (!queue_.empty()) { queue_.pop(); }
  }

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

}  // namespace aho_corasick