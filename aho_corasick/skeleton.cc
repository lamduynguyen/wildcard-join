#include "aho_corasick/skeleton.h"

#include "common/rand.h"

namespace aho_corasick {

// --------------------------------------------------------------------------------------------
void Skeleton::Segment::Insert(u16 start_pos) {
  literal_offset.emplace_back(start_pos);
  lit_off_p.emplace(start_pos, literal_offset.size() - 1);
}

auto Skeleton::Segment::Contain(u16 start_pos) const -> bool { return lit_off_p.contains(start_pos); }

auto Skeleton::Segment::IsFirstLiteral(u16 start_pos) const -> bool {
  assert(Contain(start_pos));
  return lit_off_p.at(start_pos) == 0;
}

auto Skeleton::Segment::IsLastLiteral(u16 start_pos) const -> bool {
  assert(Contain(start_pos));
  return lit_off_p.at(start_pos) == literal_offset.size() - 1;
}

auto Skeleton::Segment::IsPreviousLiteral(u16 prev_start_pos, u16 start_pos) const -> bool {
  assert(Contain(start_pos));
  auto pos = lit_off_p.at(start_pos);
  return (pos > 0) && (literal_offset[pos - 1] == prev_start_pos);
}

auto Skeleton::Segment::GetNextLiteralOffset(u16 start_pos) const -> u16 {
  assert(!IsLastLiteral(start_pos));
  auto pos = lit_off_p.at(start_pos);
  return literal_offset[pos + 1];
}

// --------------------------------------------------------------------------------------------
Skeleton::Skeleton(const char *p, u64 plen, const std::function<void(Tokenizer::TextUnit &)> &literal_fn)
    : only_wildcard_(true) {
  auto last_end_pos = 0U;
  while (true) {
    auto match = Segment();

    // Extract tokens by PERCENTAGE only
    auto prev_pos = last_end_pos;
    auto tok      = Tokenizer::NextSegment(p, plen, last_end_pos);
    if (tok.start == Tokenizer::WRONG_OFFSET) { break; }
    if (prev_pos < tok.start) { match.has_prefix_percent = true; };

    // Build between-PERCENTAGE skeleton based on the extracted token -- p[tok.start : last_end_pos]
    auto pos = tok.start;
    for (auto pos = tok.start; pos < tok.start + tok.len;) {
      auto literal = Tokenizer::NextLiteral(p, tok.start + tok.len, pos, match.max_underscore_cnt);
      if (literal.start == Tokenizer::WRONG_OFFSET) {
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
auto Skeleton::SpecialMatchEmptyPattern(const char *s, size_t slen, const char *p, size_t plen) -> bool {
  // Count how many _ and check if slen >= number of underscores
  auto underscore_cnt = 0UL;
  auto has_percent    = false;
  for (auto idx = 0UL; idx < plen; idx++) {
    if (p[idx] == Tokenizer::UNDERSCORE) {
      underscore_cnt++;
    } else if (p[idx] == Tokenizer::PERCENTAGE) {
      has_percent = true;
    }
  }
  auto no_cp = 0UL;
  for (auto ptr = s; ptr < s + slen;) {
    auto next = umbra::Utf8::readCodePoint(ptr, s + slen);
    no_cp++;
    ptr = next.next;
  }
  return (no_cp == underscore_cnt) || (no_cp > underscore_cnt && has_percent);
}

auto Skeleton::InitializeMatcher() -> Matcher {
  assert(!seg_.empty());
  return Matcher(seg_[0].max_underscore_cnt);
}

/**
 * We can only advance to the next segment if one of the following conditions is satisfied:
 * - Current segment is not the last one of the skeleton
 * - Current segment is the last one and has a PERCENTAGE suffix
 * - Current segment is the last one, doesn't have a PERCENTAGE suffix, and the AhoCorasick matcher
 *    states that the current matching is the suffix of the queried text, including suffixed underscores
 */
auto Skeleton::ValidLastLiteral(const MatchingOutputType &ac_match, u64 curr_segment_idx, const char *text,
                                u64 text_length) -> bool {
  const auto &segment  = seg_[curr_segment_idx];
  auto next_sket_index = curr_segment_idx + 1;
  if ((next_sket_index < seg_.size()) ||                                     // 1st scenario
      (next_sket_index >= seg_.size() && seg_.back().has_suffix_percent)) {  // 2nd scenario
    return true;
  }
  // Scenario 3
  // Match the suffixed underscores with the remaining code points of the text
  // TODO: Optimize this, we can interleave this check with the main `iterate.CanAdvanceOneCodePoint()` loop
  auto iterate_cur = ac_match.text_start_pos + ac_match.literal_len;
  for (auto idx = 0U; idx < segment.suffix_underscore_cnt; idx++) {
    if (iterate_cur >= text_length) { return false; }
    auto next_cp = umbra::Utf8::readCodePoint(text + iterate_cur, text + text_length);
    iterate_cur += next_cp.next - (text + iterate_cur);
  }
  return iterate_cur == text_length;
}

// --------------------------------------------------------------------------------------------
/**
 * @brief Only match within the current considerate pattern. Two key conditions:
 * - The being-matched pattern start position must be assocated with the being-matched skeleton segment
 * - The start position in text must adhere to the positional constraint of the active skeleton matcher `instance`
 */
auto Matcher::MayMatch(const Skeleton &sket, const MatchingOutputType &ac_match) -> bool {
  return segment_idx_ < sket.seg_.size() && sket[segment_idx_].Contain(ac_match.pattern_index.start_pos) &&
         ac_match.text_start_pos >= min_text_start_pos_;
}

auto Matcher::TryMatchingLiteral(const Skeleton &sket, const MatchingOutputType &ac_match, u64 curr_cp_index,
                                 DelayedMatchQueue &delay_queue) -> bool {
  if (!MayMatch(sket, ac_match)) { return false; }
  const auto &segment      = sket[segment_idx_];
  const auto diff          = ac_match.text_start_pos - ac_match.pattern_index.start_pos;
  auto next_literal_offset = 0;
  if (!segment.IsLastLiteral(ac_match.pattern_index.start_pos)) {
    next_literal_offset = segment.GetNextLiteralOffset(ac_match.pattern_index.start_pos);
  }

  if (segment.IsFirstLiteral(ac_match.pattern_index.start_pos)) {
    /**
     * @brief Two scenarios:
     * - With prefix percentage, the new 1st literal can start anywhere
     * - Without prefix percentage, the new 1st literal must start exactly at the sket's first pos
     *   Also, this scenario only happens for the 1st literal: the literal is the prefix of the text & pattern
     */
    if (segment.has_prefix_percent ||
        (segment_idx_ == 0 && ac_match.text_start_pos == ac_match.pattern_index.start_pos)) {
      // simply return true for last literal
      if (segment.IsLastLiteral(ac_match.pattern_index.start_pos)) { return true; }
      // otherwise, return the underscore count, which is used to determine the number of skipped code points
      assert(next_literal_offset >= ac_match.pattern_index.start_pos + ac_match.literal_len);
      auto underscore_cnt = next_literal_offset - ac_match.pattern_index.start_pos - ac_match.literal_len;
      delay_queue.Schedule(ac_match.pattern_index.pattern_id, underscore_cnt + curr_cp_index, next_literal_offset,
                           ac_match.pattern_index.start_pos);
      return true;
    }
  } else if (Contain(diff)) {
    // Otherwise, check if there is a previous literal that has the exact gap we are looking for
    auto prev_pat_pos = GetAndRemove(diff);
    if (segment.IsPreviousLiteral(prev_pat_pos, ac_match.pattern_index.start_pos)) {
      // simply return true for last literal
      if (segment.IsLastLiteral(ac_match.pattern_index.start_pos)) { return true; }
      // otherwise, return the underscore count, which is used to determine the number of skipped code points
      assert(next_literal_offset >= ac_match.pattern_index.start_pos + ac_match.literal_len);
      auto underscore_cnt = next_literal_offset - ac_match.pattern_index.start_pos - ac_match.literal_len;
      delay_queue.Schedule(ac_match.pattern_index.pattern_id, underscore_cnt + curr_cp_index, next_literal_offset,
                           ac_match.pattern_index.start_pos);
      return true;
    }
  }

  return false;
}

auto Matcher::AdvanceNextSegment(const Skeleton &sket, u64 min_text_next_start_pos) -> bool {
  segment_idx_++;
  min_text_start_pos_ = min_text_next_start_pos;
  if (segment_idx_ < sket.Size()) {
    auto &next_segment = sket[segment_idx_];
    match_             = LRUCache<u64, u16>(next_segment.max_underscore_cnt + 1);
    return true;
  }
  return false;
}

}  // namespace aho_corasick