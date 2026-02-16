#pragma once

#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/delay_queue.h"
#include "aho_corasick/skeleton.h"

#include <ranges>
#include <string>
#include <string_view>

namespace aho_corasick {

struct TextParserIterator;

class PatternAnalyzer {
 public:
  template <std::ranges::sized_range Range>
    requires(std::convertible_to<std::ranges::range_value_t<Range>, std::string_view> ||
             std::convertible_to<std::ranges::range_value_t<Range>, std::u8string_view>)
  PatternAnalyzer(Range &&patterns, aho_corasick::AhoCorasick &trie) {
    skeleton_.reserve(std::ranges::size(patterns));
    auto t = trie.Local();
    for (auto idx = 0U; idx < patterns.size(); idx++) {
      auto &pat = patterns[idx];
      skeleton_.emplace_back(reinterpret_cast<char *>(pat.data()), pat.size(), [&](Tokenizer::TextUnit &tok) {
        trie.Insert(reinterpret_cast<char *>(pat.data()) + tok.start, tok.len, {idx, tok.start}, t);
      });
    }
  }

  inline auto Size() const { return skeleton_.size(); }

  inline auto Skeleton(size_t i) const -> const aho_corasick::Skeleton & { return skeleton_[i]; }

 private:
  friend struct TextParserIterator;

  std::vector<aho_corasick::Skeleton> skeleton_;
};

struct TextParserIterator {
  const char *text;                              // The text being matched
  const size_t text_len;                         // Above
  const AhoCorasick *automaton;                  // The AhoCorasick automaton
  const PatternAnalyzer *build_side;             // The metadata of the build size, i.e., patterns' skeleton
  size_t text_offset;                            // Current byte offset within text
  size_t codepoint_idx;                          // The current codepoint index within text
  ART::N *ptr;                                   // AhoCorasick automaton
  DelayedMatchQueue queue;                       // Queue used to delay matching UNDERSCORE with Unicode code point
  std::vector<aho_corasick::Matcher> instances;  // Matcher to the patterns

  TextParserIterator(const char *text, size_t text_len, PatternAnalyzer *pat_side,
                     const aho_corasick::AhoCorasick *trie)
      : text(text),
        text_len(text_len),
        build_side(pat_side),
        automaton(trie),
        text_offset(0),
        codepoint_idx(0),
        ptr(trie->GetRoot()),
        queue() {
    instances.reserve(build_side->Size());
    for (auto idx = 0U; idx < build_side->Size(); idx++) {
      auto &sket = build_side->Skeleton(idx);
      if (sket.IsEmpty() || sket.OnlyWildcard()) {
        instances.emplace_back(0);
      } else {
        instances.push_back(sket.InitializeMatcher());
      }
    }
  }

  inline auto CanAdvanceOneCodePoint() { return text_offset < text_len; }

  // Two way to parse a text: One-round or iteratively
  auto ParseText() -> OutputEmitType;
  template <typename BitMap>
  void IterateOneCodePoint(BitMap &result);
  void ProcessDelayedMatching();

 private:
  auto ContinueParseText() -> OutputEmitType;
  void AppendResult(ART::N *leaf, size_t cp_len, OutputEmitType &out_result) const;
};

}  // namespace aho_corasick