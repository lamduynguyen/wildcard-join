#include "aho_corasick/skeleton.h"


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

/**
 * @brief Walk `count` code points forward from byte offset `from`.
 * @return The byte offset reached, or nullopt if the text ran out first.
 *
 * Every underscore count in a Segment is a count of code points and every
 * offset into the text is a byte offset, and the two are only the same number
 * on text that happens to be ASCII. Everything that converts one into the
 * other goes through here, instead of three callers doing the arithmetic
 * themselves.
 */
static auto Utf8Advance(const char *text, u64 text_length, u64 from, u32 count) -> std::optional<u64> {
  auto cur = from;
  for (auto idx = 0U; idx < count; idx++) {
    if (cur >= text_length) { return std::nullopt; }
    const auto next = umbra::Utf8::readCodePoint(text + cur, text + text_length);
    cur += static_cast<u64>(next.next - (text + cur));
  }
  return cur;
}

void Skeleton::FoldLiteralFreeSegments() {
  if (seg_.size() < 2) { return; }

  // Segment 0 folds forward, because it has no predecessor to fold into. Its
  // underscores become leading underscores of what follows, and the `%` that
  // separated them is what lets the merged segment still start anywhere.
  // `__%a` becomes `%__a`, and both mean "at least two code points, then a".
  //
  // Both counts, not just the suffix. A literal free segment out of the
  // constructor has everything in its suffix, but two of them in a row means
  // the second one is carrying the first one's underscores in its prefix by
  // the time it is looked at, and `__%_%aa` has to keep all three.
  while (seg_.size() > 1 && !seg_[0].HasLiteral()) {
    seg_[1].prefix_underscore_cnt += seg_[0].prefix_underscore_cnt + seg_[0].suffix_underscore_cnt;
    seg_[1].has_prefix_percent = true;
    seg_.erase(seg_.begin());
  }

  // Everything else folds backward, into the trailing underscores of its
  // predecessor. The predecessor keeps has_suffix_percent, which it already
  // has, since a `%` is what put a segment boundary there in the first place.
  for (auto idx = 1U; idx < seg_.size();) {
    if (seg_[idx].HasLiteral()) {
      idx++;
      continue;
    }
    seg_[idx - 1].suffix_underscore_cnt += seg_[idx].prefix_underscore_cnt + seg_[idx].suffix_underscore_cnt;
    seg_.erase(seg_.begin() + idx);
  }

  // The counts above moved, and max_underscore_cnt sizes the LRU cache, so it
  // has to cover them again. Too large only costs memory, too small evicts a
  // live alignment and turns into a missed match.
  for (auto &segment : seg_) {
    segment.max_underscore_cnt =
      std::max({segment.max_underscore_cnt, segment.prefix_underscore_cnt, segment.suffix_underscore_cnt});
  }
}

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

void Skeleton::ResetMatcher(Matcher &matcher) const {
  assert(!seg_.empty());
  matcher.Reset(LruCapForSegment(seg_[0]));
}

auto Skeleton::SegmentTailEnd(const MatchingOutputType &ac_match, u64 curr_segment_idx, const char *text,
                              u64 text_length) const -> std::optional<u64> {
  // The trailing `_`s impose a minimum-length tail constraint whether or not
  // the segment has a `%` after it: there must be at least
  // suffix_underscore_cnt code points between the end of the last literal and
  // the end of the text.
  return Utf8Advance(text, text_length, ac_match.text_start_pos + ac_match.literal_len,
                     seg_[curr_segment_idx].suffix_underscore_cnt);
}

/**
 * We can only satisfy the last-literal check, i.e., advance to the next segment, if:
 * - Current segment is not the last one of the skeleton, OR
 * - Current segment is the last one and has a Tokenizer::PERCENTAGE suffix, OR
 * - Current segment is the last one, has no Tokenizer::PERCENTAGE suffix, and the remaining text
 *   exactly matches the trailing underscores of this segment.
 */
auto Skeleton::ValidLastLiteral(u64 curr_segment_idx, u64 tail_end, u64 text_length) const -> bool {
  if (curr_segment_idx + 1 < seg_.size()) { return true; }  // more segments remain
  if (seg_.back().has_suffix_percent) { return true; }      // trailing % absorbs any further bytes
  return tail_end == text_length;
}

// --------------------------------------------------------------------------------------------
// Matcher
auto Matcher::TryMatchingLiteral(const Skeleton &sket, const MatchingOutputType &ac_match, u64 curr_cp_index,
                                 DelayedMatchQueue &delay_queue, const char *text, u64 text_len) -> bool {
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
    // The segment may begin no earlier than min_text_start_pos_, and its
    // leading underscores eat that many code points before the first literal
    // is allowed to start. head_end is the earliest byte offset the literal
    // can sit at. Both branches used to compare against a pattern byte offset
    // instead, which is the same number only on ASCII text.
    const auto head_end = Utf8Advance(text, text_len, min_text_start_pos_, segment.prefix_underscore_cnt);
    if (!head_end.has_value()) { return false; }
    if (segment.has_prefix_percent) {
      // With prefix %: anywhere at or after head_end.
      if (ac_match.text_start_pos < *head_end) { return false; }
    } else if (segment_idx_ != 0 || ac_match.text_start_pos != *head_end) {
      // Without: anchored, so exactly there. Only segment 0 can lack a
      // prefix %, the rest have one by construction.
      return false;
    }
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
    match_.Reset(LruCapForSegment(sket[segment_idx_]));
    return true;
  }
  return false;
}

}  // namespace aho_corasick
