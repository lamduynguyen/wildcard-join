#pragma once

#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/delay_queue.h"
#include "aho_corasick/tokenizer.h"
#include "common/lrucache.h"
#include "common/typedef.h"
#include "common/util.h"

#include "fmt/format.h"

#include <algorithm>
#include <optional>
#include <string_view>
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
  // text and text_len are here because the leading underscores of a segment
  // are a count of code points and the offsets they are checked against are
  // byte offsets, so the check has to walk the text.
  auto TryMatchingLiteral(const Skeleton &sket, const MatchingOutputType &ac_match, u64 curr_cp_index,
                          DelayedMatchQueue &delay_queue, const char *text, u64 text_len) -> bool;
  auto AdvanceNextSegment(const Skeleton &sket, u64 min_text_next_start_pos) -> bool;

  /* Misc helpers */
  inline auto CurrentSegmentIdx() { return segment_idx_; }

  inline auto GetAndRemove(u64 text_pat_diff) { return match_.GetAndRemove(text_pat_diff); }

  inline auto Contain(u64 text_pat_diff) const { return match_.Contain(text_pat_diff); }

  inline auto Upsert(u64 text_pat_diff, u64 pat_cursor) { match_.Upsert(text_pat_diff, pat_cursor); }

  // Put the matcher back in the state InitializeMatcher() would have left it
  // in, without allocating a new one. Skeleton::ResetMatcher() is the caller,
  // it is the thing that knows the capacity for segment 0.
  inline void Reset(u64 max_size) {
    segment_idx_        = 0;
    min_text_start_pos_ = 0;
    match_.Reset(max_size);
  }

 private:
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
    // Underscores before the first literal and after the last one. Counts of
    // code points, not byte distances: a `_` is one byte in the pattern but
    // stands for one code point of any width in the text, so anything that
    // turns these into a text offset has to walk the text. See Utf8Advance in
    // skeleton.cc.
    //
    // For a segment with no literals in it, suffix_underscore_cnt holds the
    // whole content and prefix_underscore_cnt is zero. Such segments are
    // folded away by FoldLiteralFreeSegments before the skeleton is used.
    u32 prefix_underscore_cnt = 0;
    u32 suffix_underscore_cnt = 0;
    u32 max_underscore_cnt    = 0;
    bool has_prefix_percent   = false;
    bool has_suffix_percent   = false;

    auto HasLiteral() const -> bool { return !literal_offset.empty(); }

    void Insert(u16 start_pos);
    auto Contain(u16 start_pos) const -> bool;
    auto IsFirstLiteral(u16 start_pos) const -> bool;
    auto IsLastLiteral(u16 start_pos) const -> bool;
    auto IsPreviousLiteral(u16 prev_start_pos, u16 start_pos) const -> bool;
    auto GetNextLiteralOffset(u16 start_pos) const -> u16;
  };

  /**
   * @param p           Raw pattern bytes.
   * @param plen        Length of pattern in bytes.
   * @param escape_str  SQL-style escape prefix (default empty = no escaping).
   * @param literal_fn  Callback invoked for each literal span. Signature:
   *                      void(Tokenizer &tok, const PatSpan &lit)
   */
  template <typename Fn>
  Skeleton(const char *p, u32 plen, std::string_view escape_str, Fn &&literal_fn) : only_wildcard_(true) {
    auto tok = Tokenizer(p, plen, escape_str);

    auto seg_idx = 0U;
    while (auto seg = tok.NextSegment()) {
      auto match               = Segment();
      match.has_prefix_percent = (seg_idx > 0) || (seg->start > 0);

      auto prev_lit_end = seg->start;
      while (auto lit = tok.NextLiteral(*seg)) {
        // Everything between two literals of a segment is unescaped `_`, one
        // byte each, because an escaped `_` is not a delimiter and ends up
        // inside the literal span. So this byte distance is an underscore
        // count.
        const auto gap = lit->start - prev_lit_end;
        if (!match.HasLiteral()) { match.prefix_underscore_cnt = gap; }
        match.max_underscore_cnt = std::max(match.max_underscore_cnt, gap);
        prev_lit_end             = lit->start + lit->len;

        match.Insert(lit->start - lit->escape_prefix_before_start);
        literal_fn(tok, *lit);
        only_wildcard_ = false;
      }

      const auto seg_end          = seg->start + seg->len;
      match.suffix_underscore_cnt = seg_end - prev_lit_end;
      match.max_underscore_cnt    = std::max(match.max_underscore_cnt, match.suffix_underscore_cnt);

      seg_.emplace_back(std::move(match));
      seg_idx++;
    }

    // Fill in has_suffix_percent for all segments
    if (!seg_.empty()) {
      if (tok.Pos() != plen) { seg_.back().has_suffix_percent = true; }
      for (int idx = static_cast<int>(seg_.size()) - 2; idx >= 0; idx--) {
        seg_[idx].has_suffix_percent = seg_[idx + 1].has_prefix_percent;
      }
    }

    // Must run after has_suffix_percent is filled in, it reads those flags.
    FoldLiteralFreeSegments();
  }

  // Convenience overload: no escaping
  template <typename Fn>
  Skeleton(const char *p, u32 plen, Fn &&literal_fn) : Skeleton(p, plen, "", std::forward<Fn>(literal_fn)) {}

  ~Skeleton() = default;

  /* Skeleton segment (i.e., structure) utilities */
  [[nodiscard]] inline auto OnlyWildcard() const noexcept { return only_wildcard_; }

  [[nodiscard]] inline auto operator[](u64 idx) const noexcept -> const Segment & { return seg_[idx]; }

  [[nodiscard]] inline auto IsEmpty() const noexcept -> bool { return seg_.empty(); }

  [[nodiscard]] inline auto Size() const noexcept { return seg_.size(); }

  inline auto Last() -> Segment & { return seg_.back(); }

  /**
   * @brief Match a pattern consisting only of `%`, `_`, and escape sequences.
   * @param p           Raw pattern bytes.
   * @param plen        Length of pattern in bytes.
   * @param s           Text to match against.
   * @param slen        Length of text in bytes.
   * @param escape_str  Same escape_str used when constructing the Skeleton.
   */
  static auto SpecialMatchEmptyPattern(const char *p, u32 plen, const char *s, size_t slen,
                                       std::string_view escape_str = "") -> bool;

  /* Skeleton utilities */
  auto InitializeMatcher() const -> Matcher;
  void ResetMatcher(Matcher &matcher) const;

  /**
   * @brief Byte offset just past this segment's trailing underscores.
   *
   * Walks suffix_underscore_cnt code points forward from the end of the
   * literal that just matched. nullopt when the text runs out first, which
   * means the segment cannot be satisfied here.
   *
   * This is both the tail check for the last segment and the earliest byte
   * offset the next segment may start at, so the two cannot drift apart.
   */
  auto SegmentTailEnd(const MatchingOutputType &ac_match, u64 curr_segment_idx, const char *text,
                      u64 text_length) const -> std::optional<u64>;

  /**
   * @brief Whether a satisfied last literal completes the whole pattern.
   * @param tail_end The offset SegmentTailEnd returned for the same match.
   */
  auto ValidLastLiteral(u64 curr_segment_idx, u64 tail_end, u64 text_length) const -> bool;

 private:
  friend class Matcher;

  /**
   * @brief Remove segments that hold underscores and no literal.
   *
   * Every advance past a segment is driven by an Aho-Corasick hit, and a
   * segment with no literal in it never produces one, so on its own such a
   * segment can never be left and the pattern can never match.
   *
   * They fold away instead of being special cased, because a `%` on both
   * sides of a run of underscores means the run can slide: `a%__%b` and
   * `a__%b` both say "a, then at least two code points, then b somewhere
   * after". The same argument at the end of the pattern makes `a%__` the
   * same as a segment for `a` with two trailing underscores and a suffix
   * `%`, which is "a followed by at least two code points".
   */
  void FoldLiteralFreeSegments();

  bool only_wildcard_;
  std::vector<Segment> seg_;
};

}  // namespace aho_corasick
