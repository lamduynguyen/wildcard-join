#include "aho_corasick/skeleton.h"

#include "common/rand.h"

namespace aho_corasick {

// --------------------------------------------------------------------------------------------
// Skeleton::Segment

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
// Skeleton::SpecialMatchEmptyPattern
// Should only be called if the pattern contains only %, _, and escape sequences.

auto Skeleton::SpecialMatchEmptyPattern(const char *p, u32 plen, const char *s, size_t slen,
                                        std::string_view escape_str) -> bool {
  // Use a lightweight Tokenizer just for IsEscaped -- no cursor state needed here
  const Tokenizer tok(p, plen, escape_str);

  auto underscore_cnt = 0U;
  auto has_percent    = false;

  for (auto i = 0U; i < plen; i++) {
    if (tok.IsEscaped(i) && (p[i] == Tokenizer::PERCENTAGE || p[i] == Tokenizer::UNDERSCORE)) {
      continue;  // escaped wildcard -- literal character, not a wildcard
    }
    if (p[i] == Tokenizer::PERCENTAGE) {
      has_percent = true;
    } else if (p[i] == Tokenizer::UNDERSCORE) {
      underscore_cnt++;
    }
  }

  // Count code points in text; each `_` matches exactly one code point
  auto no_cp = 0UL;
  for (auto ptr = s; ptr < s + slen;) {
    auto next = umbra::Utf8::readCodePoint(ptr, s + slen);
    no_cp++;
    ptr = next.next;
  }
  return (no_cp == underscore_cnt) || (no_cp > underscore_cnt && has_percent);
}

// --------------------------------------------------------------------------------------------

// Cap must accommodate every alignment that may be in match_ simultaneously.
// With a prefix `%` and repeated literals, many alignments can ripen at the
// same codepoint; bounding by max_underscore_cnt alone evicts valid entries.
static inline auto LruCapForSegment(const Skeleton::Segment &seg) -> u64 {
  u64 cap = seg.max_underscore_cnt + 1;
  if (!seg.literal_offset.empty()) { cap = std::max<u64>(cap, static_cast<u64>(seg.literal_offset.back()) + 1); }
  return cap;
}

auto Skeleton::InitializeMatcher() const -> Matcher {
  assert(!seg_.empty());
  return Matcher(LruCapForSegment(seg_[0]));
}

/**
 * We can only satisfy the last-literal check, i.e., advance to the next segment, if:
 * - Current segment is not the last one of the skeleton, OR
 * - Current segment is the last one and has a Tokenizer::PERCENTAGE suffix, OR
 * - Current segment is the last one, has no Tokenizer::PERCENTAGE suffix, and the remaining text
 *   exactly matches the trailing underscores of this segment.
 */
auto Skeleton::ValidLastLiteral(const MatchingOutputType &ac_match, u64 curr_segment_idx, const char *text,
                                u64 text_length) const -> bool {
  const auto &segment  = seg_[curr_segment_idx];
  auto next_sket_index = curr_segment_idx + 1;

  if (next_sket_index < seg_.size()) { return true; }  // more segments remain

  // Even with a trailing `%`, the trailing `_`s impose a minimum-length tail
  // constraint: there must be ≥ suffix_underscore_cnt codepoints between the
  // end of the last literal and the end of the text. Without `%`, the count
  // must match exactly.
  auto iterate_cur = ac_match.text_start_pos + ac_match.literal_len;
  for (auto idx = 0U; idx < segment.suffix_underscore_cnt; idx++) {
    if (iterate_cur >= text_length) { return false; }
    auto next_cp = umbra::Utf8::readCodePoint(text + iterate_cur, text + text_length);
    iterate_cur += next_cp.next - (text + iterate_cur);
  }
  if (seg_.back().has_suffix_percent) { return true; }  // trailing % absorbs any further bytes
  return iterate_cur == text_length;
}

// --------------------------------------------------------------------------------------------
// Matcher
auto Matcher::TryMatchingLiteral(const Skeleton &sket, const MatchingOutputType &ac_match, u64 curr_cp_index,
                                 DelayedMatchQueue &delay_queue) -> bool {
  const auto &segment = sket[segment_idx_];
  const auto pat_pos  = ac_match.pattern_index.start_pos;

  // Guard: this AC hit must belong to the current segment and respect the min text position
  if (segment_idx_ >= sket.seg_.size() || !segment.Contain(pat_pos) || ac_match.text_start_pos < min_text_start_pos_) {
    return false;
  }

  // Validate positional fit:
  // - First literal -- anchored at text start (no prefix %), or freely placed (with prefix %)
  // - Non-first literal -- previous literal must exist at the exact expected gap (diff)
  if (segment.IsFirstLiteral(pat_pos)) {
    // With prefix %: can start anywhere. Without: must align exactly with text start.
    const bool anchored_at_text_start = segment_idx_ == 0 && ac_match.text_start_pos == pat_pos;
    if (!segment.has_prefix_percent && !anchored_at_text_start) { return false; }
  } else {
    const auto diff = ac_match.text_start_pos - pat_pos;
    if (!Contain(diff)) { return false; }
    if (!segment.IsPreviousLiteral(GetAndRemove(diff), pat_pos)) { return false; }
  }

  // Schedule the next literal's window if this is not the last literal in the segment.
  // The delay = number of `_` between this literal and the next literal/end of pattern.
  if (!segment.IsLastLiteral(pat_pos)) {
    const auto next_offset    = segment.GetNextLiteralOffset(pat_pos);
    const auto underscore_cnt = next_offset - pat_pos - ac_match.literal_len;
    assert(next_offset >= pat_pos + ac_match.literal_len);
    delay_queue.Schedule(ac_match.pattern_index.pattern_id, underscore_cnt + curr_cp_index, next_offset, pat_pos);
  }

  return true;
}

auto Matcher::AdvanceNextSegment(const Skeleton &sket, u64 min_text_next_start_pos) -> bool {
  segment_idx_++;
  min_text_start_pos_ = min_text_next_start_pos;
  if (segment_idx_ < sket.Size()) {
    match_ = LRUCache<u64, u16>(LruCapForSegment(sket[segment_idx_]));
    return true;
  }
  return false;
}

}  // namespace aho_corasick
