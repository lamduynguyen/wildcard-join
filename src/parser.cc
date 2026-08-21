#include "aho_corasick/parser.h"

namespace aho_corasick {

auto TextParserIterator::ParseText() -> OutputEmitType {
  OutputEmitType result;
  while (CanAdvanceOneCodePoint()) {
    auto matches = ContinueParseText();
    result.merge(matches);
  }
  return result;
}

template <typename BitMap>
void TextParserIterator::IterateOneCodePoint(BitMap &result) {
  const auto end_offset = text_offset;
  const auto ac_matches = ContinueParseText();

  for (const auto &match : ac_matches) {
    const auto pat_id   = match.pattern_index.pattern_id;
    const auto &sket    = build_side->GetSkeleton(pat_id);
    auto &matcher       = instances[pat_id];
    const auto &segment = sket[matcher.CurrentSegmentIdx()];

    if (result[pat_id]) { continue; }

    if (!matcher.TryMatchingLiteral(sket, match, codepoint_idx, queue)) { continue; }

    if (segment.IsLastLiteral(match.pattern_index.start_pos) &&
        sket.ValidLastLiteral(match, matcher.CurrentSegmentIdx(), text, text_len) &&
        !matcher.AdvanceNextSegment(sket, end_offset + segment.suffix_underscore_cnt + 1)) {
      result[pat_id] = true;
    }
  }
}

auto TextParserIterator::ContinueParseText() -> OutputEmitType {
  OutputEmitType result;

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
  return result;
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
    out_result.emplace(pattern_idx, literal_len, match_pos);
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
