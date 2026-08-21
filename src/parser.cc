#include "aho_corasick/parser.h"

namespace aho_corasick {

TextParserIterator::TextParserIterator(const PatternAnalyzer *build_side, const AhoCorasick *trie)
    : text(nullptr),
      text_len(0),
      automaton(trie),
      build_side(build_side),
      text_offset(0),
      codepoint_idx(0),
      ptr(trie->GetRoot()),
      queue() {
  const auto pattern_cnt = build_side->Size();
  instances.reserve(pattern_cnt);
  for (auto idx = 0U; idx < pattern_cnt; idx++) {
    const auto &sket = build_side->GetSkeleton(idx);
    if (sket.IsEmpty() || sket.OnlyWildcard()) {
      // No literal, so nothing is ever inserted in the trie for it and no AC
      // hit can name it. The entry only exists to keep indices aligned.
      instances.emplace_back(0);
    } else {
      instances.push_back(sket.InitializeMatcher());
    }
  }
  dirty_.assign(pattern_cnt, 0);
  dirty_list_.reserve(64);
}

void TextParserIterator::ResetText(const char *next_text, size_t next_text_len) {
  for (auto pattern_id : dirty_list_) {
    build_side->GetSkeleton(pattern_id).ResetMatcher(instances[pattern_id]);
    dirty_[pattern_id] = 0;
  }
  dirty_list_.clear();
  queue.Clear();

  text          = next_text;
  text_len      = next_text_len;
  text_offset   = 0;
  codepoint_idx = 0;
  ptr           = automaton->GetRoot();
}

auto TextParserIterator::ParseText() -> OutputEmitType {
  OutputEmitType result;
  OutputEmitType per_codepoint;
  while (CanAdvanceOneCodePoint()) {
    ContinueParseText(per_codepoint);
    result.insert(result.end(), per_codepoint.begin(), per_codepoint.end());
  }
  return result;
}

template <typename BitMap>
void TextParserIterator::IterateOneCodePoint(BitMap &result) {
  const auto end_offset = text_offset;
  ContinueParseText(emit_buffer_);

  for (const auto &match : emit_buffer_) {
    const auto pat_id = match.pattern_index.pattern_id;
    if (result[pat_id]) { continue; }

    const auto &sket = build_side->GetSkeleton(pat_id);
    auto &matcher    = instances[pat_id];
    // Bound after the result check, not before it. A pattern that already
    // matched has walked segment_idx_ one past the last segment, so binding
    // sket[CurrentSegmentIdx()] first forms a reference one past the end of
    // the segment vector. Nothing read through it, but it is still out of
    // range and there is no reason to write it that way.
    const auto &segment = sket[matcher.CurrentSegmentIdx()];

    // Before the call, not after a successful one: TryMatchingLiteral can
    // consume an LRU entry and still return false.
    MarkDirty(pat_id);
    if (!matcher.TryMatchingLiteral(sket, match, codepoint_idx, queue)) { continue; }

    if (segment.IsLastLiteral(match.pattern_index.start_pos) &&
        sket.ValidLastLiteral(match, matcher.CurrentSegmentIdx(), text, text_len) &&
        !matcher.AdvanceNextSegment(sket, end_offset + segment.suffix_underscore_cnt + 1)) {
      result[pat_id] = true;
    }
  }
}

void TextParserIterator::ContinueParseText(OutputEmitType &result) {
  result.clear();

  const auto *cp     = text + text_offset;
  const auto cp_info = umbra::Utf8::readCodePoint(cp, text + text_len);
  const auto cp_len  = cp_info.next - cp;

  auto next = AhoCorasick::VisitCodePoint(ptr, cp, cp_len);
  while (ptr != automaton->GetRoot() && next == nullptr) {
    ptr  = ptr->getSuffixLink();
    next = AhoCorasick::VisitCodePoint(ptr, cp, cp_len);
  }
  assert(next != nullptr || ptr == automaton->GetRoot());

  if (next != nullptr) {
    ptr = next;
    if (ptr->isTerminalNode()) {
      auto leaf = ptr->getChild(NULL_TERMINATOR);
      AppendResult(leaf, cp_len, result);
    }
  }

  for (auto out = ptr->getOutputLink(); out != nullptr; out = out->getOutputLink()) {
    if (out->isTerminalNode()) {
      auto leaf = out->getChild(NULL_TERMINATOR);
      AppendResult(leaf, cp_len, result);
    }
  }

  text_offset += cp_len;
  codepoint_idx += 1;

#ifndef NDEBUG
  // This used to be a hash set, and dropping it to a vector is only safe if
  // one code point cannot emit the same match twice. The argument is in the
  // comment on OutputEmitType. This is that argument as an assertion, so it
  // gets checked on every debug and sanitizer run instead of being trusted.
  // Quadratic, but the emit list at one code point is a handful of entries.
  for (size_t i = 0; i < result.size(); i++) {
    for (size_t j = i + 1; j < result.size(); j++) { assert(!(result[i] == result[j])); }
  }
#endif
}

void TextParserIterator::AppendResult(ART::N256 *leaf, size_t cp_len, OutputEmitType &out_result) const {
  assert(ART::N256::isLeaf(leaf));
  const auto keyword_id = ART::N256::getLeaf(leaf)->auxIndex;

  auto it = automaton->literal_map_.find(keyword_id);
  assert(it != automaton->literal_map_.end());

  const auto literal_len = it->second.literal_len;
  const auto match_pos   = text_offset - literal_len + cp_len;

  for (auto value : it->second.bitmap) {
    auto pattern_idx = PatternIndexType::FromUint(value);
    out_result.emplace_back(pattern_idx, literal_len, match_pos);
  }
}

void TextParserIterator::ProcessDelayedMatching() {
  while (queue.FrontReady(codepoint_idx)) {
    const auto &item = queue.Front();
    instances[item.pattern_id].Upsert(text_offset - item.next_pattern_pos, item.prev_pattern_pos);
    queue.Pop();
  }
}

template void TextParserIterator::IterateOneCodePoint<std::vector<bool>>(std::vector<bool> &);

}  // namespace aho_corasick
