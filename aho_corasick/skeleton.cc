#include "aho_corasick/skeleton.h"

#include "common/rand.h"
#include "fmt/format.h"

namespace aho_corasick {

// --------------------------------------------------------------------------------------------
void Skeleton::Segment::Insert(pat_off_t start_pos) {
  literal_offset.emplace_back(start_pos);
  lit_off_p.emplace(start_pos, literal_offset.size() - 1);
}

auto Skeleton::Segment::Contain(pat_off_t start_pos) const -> bool { return lit_off_p.contains(start_pos); }

auto Skeleton::Segment::IsFirstLiteral(pat_off_t start_pos) const -> bool {
  assert(Contain(start_pos));
  return lit_off_p.at(start_pos) == 0;
}

auto Skeleton::Segment::IsLastLiteral(pat_off_t start_pos) const -> bool {
  assert(Contain(start_pos));
  return lit_off_p.at(start_pos) == literal_offset.size() - 1;
}

auto Skeleton::Segment::IsPreviousLiteral(pat_off_t prev_start_pos, pat_off_t start_pos) const -> bool {
  assert(Contain(start_pos));
  auto pos = lit_off_p.at(start_pos);
  return (pos > 0) && (literal_offset[pos - 1] == prev_start_pos);
}

auto Skeleton::Segment::GetNextLiteralOffset(pat_off_t start_pos) -> pat_off_t {
  assert(!IsLastLiteral(start_pos));
  auto pos = lit_off_p.at(start_pos);
  return literal_offset[pos + 1];
}

// --------------------------------------------------------------------------------------------
Skeleton::Skeleton(const char *p, u64 plen, const std::function<void(Token &)> &literal_fn) : only_wildcard_(true) {
  auto last_end_pos = 0U;
  while (true) {
    auto match = Segment();

    // Extract tokens by PERCENTAGE only
    auto prev_pos = last_end_pos;
    auto tok      = Token::NextSegment(p, plen, last_end_pos);
    if (tok.start == Token::WRONG_OFFSET) { break; }
    if (prev_pos < tok.start) { match.has_prefix_percent = true; };

    // Build between-PERCENTAGE skeleton based on the extracted token -- p[tok.start : last_end_pos]
    auto pos = tok.start;
    for (auto pos = tok.start; pos < last_end_pos;) {
      auto literal = Token::NextToken(p, plen, pos, match.max_underscore_cnt);
      if (literal.start == Token::WRONG_OFFSET) {
        // this means we have a suffix _ scenario
        match.suffix_underscore_cnt = last_end_pos - pos;
        break;
      }
      match.Insert(literal.start);
      literal_fn(literal);
      only_wildcard_ = false;
    }
    seg_.emplace_back(std::move(match));
  }
  // Fill in `has_suffix_percent` info for all segment
  if (!seg_.empty()) {
    if (last_end_pos != plen) { seg_.back().has_suffix_percent = true; }
    for (int idx = seg_.size() - 2; idx >= 0; idx--) {
      seg_[idx].has_suffix_percent = seg_[idx + 1].has_prefix_percent;
    }
  }
}

// This should only be called if the pattern only contains % and _
auto Skeleton::SpecialMatchEmptyPattern(size_t slen, const char *p, size_t plen) -> bool {
  // Count how many _ and check if slen >= number of underscores
  auto underscore_cnt = 0UL;
  auto has_percent    = false;
  for (auto idx = 0UL; idx < plen; idx++) {
    if (p[idx] == UNDERSCORE) {
      underscore_cnt++;
    } else if (p[idx] == PERCENTAGE) {
      has_percent = true;
    }
  }
  return (slen == underscore_cnt) || (slen > underscore_cnt && has_percent);
}

/**
 * @brief Only match within the current considerate pattern. Two key conditions:
 * - The being-matched pattern start position must be assocated with the being-matched skeleton segment
 * - The start position in text must adhere to the positional constraint of the active skeleton matcher `instance`
 */
auto Skeleton::MayMatch(const MatchingOutputType &ac_match, Matcher &matcher) -> bool {
  return matcher.segment_idx_ < seg_.size() && seg_[matcher.segment_idx_].Contain(ac_match.pattern_index.start_pos) &&
         ac_match.text_start_pos >= matcher.min_text_start_pos_;
}

auto Skeleton::TryMatching(const MatchingOutputType &ac_match, Matcher &matcher) -> bool {
  const auto &segment = seg_[matcher.segment_idx_];
  const auto diff     = ac_match.text_start_pos - ac_match.pattern_index.start_pos;

  if (segment.IsFirstLiteral(ac_match.pattern_index.start_pos)) {
    assert(!matcher.Contain(diff));
    /**
     * @brief Two scenarios:
     * - With prefix percentage, the new 1st literal can start anywhere
     * - Without prefix percentage, the new 1st literal must start exactly at the sket's first pos
     *   Also, this scenario only happens for the 1st literal and the literal is the prefix of the text & pattern
     */
    if (segment.has_prefix_percent ||
        (matcher.segment_idx_ == 0 && ac_match.text_start_pos == ac_match.pattern_index.start_pos)) {
      matcher.Upsert(diff, ac_match.pattern_index.start_pos);
      return true;
    }
  } else if (matcher.Contain(diff)) {
    // Otherwise, check if there is a previous literal that has the exact gap we are looking for
    auto prev_pat_pos  = matcher.Get(diff);
    auto prev_text_idx = prev_pat_pos + diff;
    assert(ac_match.text_start_pos >= prev_text_idx);
    if (segment.IsPreviousLiteral(prev_pat_pos, ac_match.pattern_index.start_pos) &&
        ac_match.text_start_pos - prev_text_idx == ac_match.pattern_index.start_pos - prev_pat_pos) {
      matcher.Upsert(diff, ac_match.pattern_index.start_pos);
      return true;
    }
  }

  return false;
}

/**
 * We can only advance to the next sket, i.e., satisfy current matcher, if one of the following conditions is satisfied:
 * - Current segment is not the last one of the skeleton
 * - Current segment is the last one and has a suffix aho_corasick::PERCENTAGE
 * - Current segment is the last one, doesn't have a suffix aho_corasick::PERCENTAGE, and the AhoCorasick matcher
 *    states that the current matching is the suffix of the queried text, including suffixed underscores
 */
auto Skeleton::SatisfyMatcher(const MatchingOutputType &ac_match, const Matcher &matcher, u64 text_length) -> bool {
  const auto &segment  = seg_[matcher.segment_idx_];
  auto next_sket_index = matcher.segment_idx_ + 1;
  return ((next_sket_index < seg_.size()) || (next_sket_index >= seg_.size() && seg_.back().has_suffix_percent) ||
          (next_sket_index >= seg_.size() && !seg_.back().has_suffix_percent &&
           ac_match.text_start_pos + ac_match.literal_len + segment.suffix_underscore_cnt == text_length));
}

auto Skeleton::InitializeMatcher() -> Matcher {
  assert(!seg_.empty());
  return Matcher(seg_[0].max_underscore_cnt);
}

auto Skeleton::AdvanceNextSegment(u64 min_text_next_start_pos, Matcher &matcher) -> bool {
  matcher.segment_idx_++;
  matcher.min_text_start_pos_ = min_text_next_start_pos;
  if (matcher.segment_idx_ < seg_.size()) {
    auto &next_segment = seg_[matcher.segment_idx_];
    matcher.match_     = LRUCache<u64, pat_off_t>(next_segment.max_underscore_cnt + 1);
    return true;
  }
  return false;
}

// --------------------------------------------------------------------------------------------
auto Token::NextToken(const char *s, size_t slen, u32 &pos, u32 &max_underscore_cnt) -> Token {
  auto prev_pos = pos;
  while (pos < slen && IsDelim(s[pos])) { pos++; }
  max_underscore_cnt = std::max(max_underscore_cnt, pos - prev_pos);
  if (pos >= slen) {
    // no more tokens; resetting pos back to suffix processing
    pos = prev_pos;
    return {WRONG_OFFSET, 0};
  }
  auto start = pos;
  while (pos < slen && !IsDelim(s[pos])) { pos++; }
  return {start, pos - start};
};

auto Token::NextSegment(const char *s, size_t slen, u32 &pos) -> Token {
  auto prev_pos = pos;
  while (pos < slen && s[pos] == PERCENTAGE) { pos++; }
  if (pos >= slen) {
    // no more tokens; resetting pos back to suffix processing
    pos = prev_pos;
    return {WRONG_OFFSET, 0};
  }
  auto start = pos;
  while (pos < slen && s[pos] != PERCENTAGE) { pos++; }
  return {start, pos - start};
};

}  // namespace aho_corasick