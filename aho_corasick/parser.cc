#include "aho_corasick/parser.h"

namespace aho_corasick {

auto TextParserIterator::ParseText() -> OutputEmitType {
  /**
   * Output link logic:
   * - In standard AhoCorasick, following suffix links leads to a node with a NULL_TERMINATOR
   *   child -- this is an output link.
   * - Here we store output links directly as trie leaves (NULL_TERMINATOR children), so we
   *   collect them by traversing getOutputLink() chains.
   */
  OutputEmitType result;
  while (CanAdvanceOneCodePoint()) {
    auto matches = ContinueParseText();
    result.merge(matches);
  }
  return result;
}

template <typename BitMap>
void TextParserIterator::IterateOneCodePoint(BitMap &result) {
  const auto end_offset = text_offset;  // byte offset before advancing
  const auto ac_matches = ContinueParseText();

  for (const auto &match : ac_matches) {
    const auto pat_id   = match.pattern_index.pattern_id;
    const auto &sket    = build_side->GetSkeleton(pat_id);
    auto &matcher       = instances[pat_id];
    const auto &segment = sket[matcher.CurrentSegmentIdx()];

    if (result[pat_id]) { continue; }  // already matched, skip

    if (!matcher.TryMatchingLiteral(sket, match, codepoint_idx, queue)) { continue; }

    // Check whether this literal completes the current segment and, if so,
    // whether the full pattern is now satisfied
    if (segment.IsLastLiteral(match.pattern_index.start_pos) &&
        sket.ValidLastLiteral(match, matcher.CurrentSegmentIdx(), text, text_len) &&
        !matcher.AdvanceNextSegment(sket, end_offset + segment.suffix_underscore_cnt + 1)) {
      result[pat_id] = true;
    }
  }
}

auto TextParserIterator::ContinueParseText() -> OutputEmitType {
  OutputEmitType result;

  // Read the next UTF-8 code point
  const auto *cp     = text + text_offset;
  const auto cp_info = umbra::Utf8::readCodePoint(cp, text + text_len);
  const auto cp_len  = cp_info.next - cp;

  // Advance the AhoCorasick automaton:
  //   1. No transition from current node → follow suffix links back toward root
  //   2. Transition leads to a terminal node → record match and continue
  //   3. Transition leads to an internal node → move forward
  auto next = AhoCorasick::VisitCodePoint(ptr, cp, cp_len);
  while (ptr != automaton->GetRoot() && next == nullptr) {
    ptr  = ART::N::getSuffixLink(ptr);
    next = AhoCorasick::VisitCodePoint(ptr, cp, cp_len);
  }
  assert(next != nullptr || ptr == automaton->GetRoot());

  if (next != nullptr) {
    ptr = next;
    if (ptr->isTerminalNode()) {
      auto leaf = ART::N::getChild(NULL_TERMINATOR, ptr);
      AppendResult(leaf, cp_len, result);
    }
  }

  // Follow output-link chain to collect all patterns that end here
  for (auto *out = ART::N::getOutputLink(ptr); out != nullptr; out = ART::N::getOutputLink(out)) {
    if (out->isTerminalNode()) {
      auto leaf = ART::N::getChild(NULL_TERMINATOR, out);
      AppendResult(leaf, cp_len, result);
    }
  }

  text_offset += cp_len;
  codepoint_idx += 1;
  return result;
}

void TextParserIterator::AppendResult(ART::N *leaf, size_t cp_len, OutputEmitType &out_result) const {
  assert(ART::N::isLeaf(leaf));
  const auto *node       = ART::N::getLeaf(leaf);
  const auto keyword_id  = node->auxIndex;
  const auto literal_len = node->keyLenWithoutNullTerminator();
  const auto match_pos   = text_offset - literal_len + cp_len;

  auto it = automaton->literal_map_.find(keyword_id);
  assert(it != automaton->literal_map_.end());
  for (auto value : it->second) {
    auto pattern_idx = PatternIndexType::FromUint(value);
    out_result.emplace(pattern_idx, literal_len, match_pos);
  }
}

// ---- ProcessDelayedMatching -------------------------------------------------

void TextParserIterator::ProcessDelayedMatching() {
  while (queue.FrontReady(codepoint_idx)) {
    const auto &item = queue.Front();
    instances[item.pattern_id].Upsert(text_offset - item.next_pattern_pos, item.prev_pattern_pos);
    queue.Pop();
  }
}

// ---- Explicit instantiations ------------------------------------------------

template void TextParserIterator::IterateOneCodePoint<std::vector<bool>>(std::vector<bool> &);

}  // namespace aho_corasick