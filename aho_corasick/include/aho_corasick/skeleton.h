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

class Skeleton {
 public:
  struct Matcher {
    u64 segment_idx        = 0;  // The idx of the skeleton segment
    u64 min_text_start_pos = 0;  // The min starting offset in text that we can continue matching for seg[segment_idx]
    ska::flat_hash_map<u64, u64> match;  // A mapping of (text_cur - pat_cur) => pat_cur

    inline auto AdvanceNextSegment(u64 min_text_next_start_pos) {
      segment_idx++;
      min_text_start_pos = min_text_next_start_pos;
      match.clear();
    }

    inline auto Contain(u64 text_pat_diff) const { return match.contains(text_pat_diff); }

    inline auto Insert(u64 text_pat_diff, u64 pat_cursor) { match.emplace(text_pat_diff, pat_cursor); }

    inline auto operator[](u64 text_pat_diff) -> u64 & { return match[text_pat_diff]; }
  };

  struct Segment {
    ska::flat_hash_map<u64, u64> prev_literal_offset;  // Map from literal's start pos to that of previous literal
    u64 last_literal_start_pos;
    u64 suffix_underscore_cnt;
    bool has_prefix_percent;
    bool has_suffix_percent;

    auto Contain(u64 start_pos) const -> bool;
    auto IsFirstLiteral(u64 start_pos) const -> bool;
    auto IsLastLiteral(u64 start_pos) const -> bool;
    auto IsPreviousLiteral(u64 prev_start_pos, u64 start_pos) const -> bool;
  };

  Skeleton(const char *p, size_t plen, const std::function<void(Token &)> &literal_fn);
  ~Skeleton() = default;

  inline auto OnlyWildcard() { return only_wildcard_; }

  inline auto operator[](int idx) -> Segment & { return seg_[idx]; }

  inline auto IsEmpty() -> bool { return seg_.empty(); }

  inline auto Size() { return seg_.size(); }

  inline auto Last() -> Segment & { return seg_.back(); }

  static auto SpecialMatchEmptyPattern(size_t slen, const char *p, size_t plen) -> bool;

  auto TryMatching(const MatchingOutputType &ac_match, Matcher &matcher) -> bool;
  auto SatisfyMatcher(const MatchingOutputType &ac_match, const Matcher &matcher, u64 text_length) -> bool;

 private:
  bool only_wildcard_;
  std::vector<Segment> seg_;
};

}  // namespace aho_corasick