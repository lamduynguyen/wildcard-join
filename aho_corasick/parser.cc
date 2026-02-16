#include "aho_corasick/parser.h"

namespace aho_corasick {

auto TextParserIterator::ParseText() -> OutputEmitType {
  /**
   * Output link logic
   * - In the original AhoCorasick, when following the suffix links, we meet an ART node whose has a NULL TERMINATOR
   *    => this is an output link
   * - In our implementation, we directly store all output links (the NULL TERMINATOR node) as the trie leaf
   */
  OutputEmitType result;
  while (CanAdvanceOneCodePoint()) {
    auto next_set = ContinueParseText();
    result.merge(next_set);
  }
  return result;
}

template <typename BitMap>
void TextParserIterator::IterateOneCodePoint(BitMap &result) {
  auto end_offset  = text_offset;
  auto ac_matchers = ContinueParseText();

  for (auto &match : ac_matchers) {
    auto pat_id   = match.pattern_index.pattern_id;
    auto &sket    = build_side->Skeleton(pat_id);
    auto &matcher = instances[pat_id];
    auto &segment = sket[matcher.CurrentSegmentIdx()];

    // #4.1. Check the possible matched literals
    if (!result[pat_id] && matcher.TryMatchingLiteral(sket, match, codepoint_idx, queue)) {
      // Now, check if we just insert the last match of the segment
      if (segment.IsLastLiteral(match.pattern_index.start_pos) &&
          sket.ValidLastLiteral(match, matcher.CurrentSegmentIdx(), text, text_len) &&
          !matcher.AdvanceNextSegment(sket, end_offset + segment.suffix_underscore_cnt + 1)) {
        result[pat_id] = true;
      }
    }
  }
}

// The caller
auto TextParserIterator::ContinueParseText() -> OutputEmitType {
  OutputEmitType result;

  // Retrieve next code point
  auto cp         = text + text_offset;
  auto advance_cp = umbra::Utf8::readCodePoint(cp, text + text_len);
  auto cp_len     = advance_cp.next - cp;

  // Three cases:
  //  1. If the next possible state is a nullptr, we go back to root
  //  2. If the next possible state is a leaf (due to lazy expansive + end all keywords as NULL terminator),
  //      we go back to root and output that pattern
  //  3. Otherwise, move forward to that state
  auto possible_next = AhoCorasick::VisitCodePoint(ptr, cp, cp_len);
  while (ptr != automaton->GetRoot() && possible_next == nullptr) {
    ptr           = ART::N::getSuffixLink(ptr);
    possible_next = AhoCorasick::VisitCodePoint(ptr, cp, cp_len);
  }
  assert((possible_next != nullptr) || (ptr == automaton->GetRoot()));  // assertion for case #1
  if (possible_next != nullptr) {
    // case #2 & #3
    ptr = possible_next;
    if (ptr->isTerminalNode()) {
      // case #2: matching for 2nd case
      auto leaf = ART::N::getChild(NULL_TERMINATOR, possible_next);
      AppendResult(leaf, cp_len, result);
    }
  }
  // Evaluate output links
  auto output_link = ART::N::getOutputLink(ptr);
  while (output_link != nullptr) {
    if (output_link->isTerminalNode()) {
      auto leaf = ART::N::getChild(NULL_TERMINATOR, output_link);
      AppendResult(leaf, cp_len, result);
    }
    output_link = ART::N::getOutputLink(output_link);  // follow the suffix-link chain
  }

  // Advance next offset in the text for next processing
  text_offset += cp_len;
  codepoint_idx++;
  return result;
}

void TextParserIterator::AppendResult(ART::N *leaf, size_t cp_len, OutputEmitType &out_result) const {
  assert(ART::N::isLeaf(leaf));
  auto keyword_id  = ART::N::getLeaf(leaf)->auxIndex;
  auto literal_len = ART::N::getLeaf(leaf)->keyLenWithoutNullTerminator();
  auto match_pos   = text_offset - literal_len + cp_len;
  auto it          = automaton->literal_map_.find(keyword_id);
  assert(it != automaton->literal_map_.end());
  const auto &bitmap = it->second;
  for (auto value : bitmap) {
    auto pattern_idx = PatternIndexType::FromUint(value);
    out_result.emplace(pattern_idx, literal_len, match_pos);
  }
}

void TextParserIterator::ProcessDelayedMatching() {
  while (queue.FrontReady(codepoint_idx)) {
    const auto &item = queue.Front();
    auto &matcher    = instances[item.pattern_id];
    fmt::println("Insert matching: [diff: {}, next_start_pos: {}, prev_start_pos: {}]",
                 text_offset - item.next_pattern_pos, item.next_pattern_pos, item.prev_pattern_pos);
    matcher.Upsert(text_offset - item.next_pattern_pos, item.prev_pattern_pos);
    queue.Pop();
  }
}

template void TextParserIterator::IterateOneCodePoint<std::vector<bool>>(std::vector<bool> &);

}  // namespace aho_corasick