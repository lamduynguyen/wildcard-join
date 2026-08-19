#pragma once

#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/delay_queue.h"
#include "aho_corasick/skeleton.h"

#include <ranges>
#include <string>
#include <string_view>

namespace aho_corasick {

struct TextParserIterator;

/**
 * @brief Owns the compiled skeletons for a set of wildcard patterns and drives
 * trie insertion during the build phase.
 *
 * Constructed once per query; shared (read-only) with all TextParserIterators.
 */
class PatternAnalyzer {
 public:
  template <std::ranges::sized_range Range>
    requires(std::convertible_to<std::ranges::range_value_t<Range>, std::string_view> ||
             std::convertible_to<std::ranges::range_value_t<Range>, std::u8string_view>)
  PatternAnalyzer(Range &&patterns, std::string_view escape_str, AhoCorasick &trie) {
    skeleton_.reserve(std::ranges::size(patterns));
    for (auto idx = 0U; idx < patterns.size(); idx++) {
      auto &pat  = patterns[idx];
      auto *data = reinterpret_cast<char *>(pat.data());
      skeleton_.emplace_back(data, static_cast<u32>(pat.size()), escape_str,
                             [&](Tokenizer &tok, const LiteralSpan &lit) {
                               std::vector<char> buf(lit.len + 1, '\0');
                               auto sv = tok.StripEscapes(lit, buf.data());
                               trie.Insert(sv.data(), sv.size(), {idx, lit.start - lit.escape_prefix_before_start});
                             });
    }
  }

  inline auto Size() const -> size_t { return skeleton_.size(); }

  inline auto GetSkeleton(size_t i) const -> const Skeleton & { return skeleton_[i]; }

 private:
  friend struct TextParserIterator;

  std::vector<Skeleton> skeleton_;
};

/**
 * @brief Stateful iterator that walks the text one Unicode code point at a time,
 * driving both the AhoCorasick automaton and the per-pattern Skeleton matchers.
 *
 * One TextParserIterator is created per text string to match; the PatternAnalyzer
 * (build side) is shared and read-only.
 */
struct TextParserIterator {
  // ---- Construction --------------------------------------------------------

  TextParserIterator(const char *text, size_t text_len, const PatternAnalyzer *build_side, const AhoCorasick *trie)
      : text(text),
        text_len(text_len),
        automaton(trie),
        build_side(build_side),
        text_offset(0),
        codepoint_idx(0),
        ptr(trie->GetRoot()),
        queue() {
    instances.reserve(build_side->Size());
    for (auto idx = 0U; idx < build_side->Size(); idx++) {
      const auto &sket = build_side->GetSkeleton(idx);
      if (sket.IsEmpty() || sket.OnlyWildcard()) {
        instances.emplace_back(0);
      } else {
        instances.push_back(sket.InitializeMatcher());
      }
    }
  }

  inline auto CanAdvanceOneCodePoint() const -> bool { return text_offset < text_len; }

  /**
   * @brief Consume the entire text in one pass and return all AC matches.
   * Used when the caller only needs the raw AhoCorasick output (no skeleton filtering).
   */
  auto ParseText() -> OutputEmitType;

  /**
   * @brief Advance one code point, run AC + skeleton matching, and update `result`.
   * Call ProcessDelayedMatching() after each code point to flush the underscore queue.
   */
  template <typename BitMap>
  void IterateOneCodePoint(BitMap &result);

  /**
   * @brief Flush any delayed underscore matches whose code-point deadline has been reached.
   * Must be called after each IterateOneCodePoint() call.
   */
  void ProcessDelayedMatching();

  // ---- State ---------------------------------------------------------------

  const char *text;                   // text being matched
  const size_t text_len;              // byte length of text
  const AhoCorasick *automaton;       // AhoCorasick automaton (read-only)
  const PatternAnalyzer *build_side;  // compiled pattern skeletons (read-only)
  size_t text_offset;                 // current byte offset within text
  size_t codepoint_idx;               // current code-point index within text
  ART::N256 *ptr;                     // current AhoCorasick automaton node
  DelayedMatchQueue queue;            // deferred underscore-to-codepoint matching
  std::vector<Matcher> instances;     // per-pattern matcher state

 private:
  /**
   * @brief Advance the automaton by one code point and return all AC matches ending here.
   * Updates text_offset and codepoint_idx.
   */
  auto ContinueParseText() -> OutputEmitType;

  void AppendResult(ART::N256 *leaf, size_t cp_len, OutputEmitType &out_result) const;
};

}  // namespace aho_corasick
