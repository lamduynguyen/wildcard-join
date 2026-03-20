#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/parser.h"
#include "aho_corasick/skeleton.h"
#include "common/utf8.h"
#include "fmt/format.h"

#include <cstdlib>
#include <deque>
#include <queue>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

using aho_corasick::Tokenizer;
using namespace std::string_view_literals;

bool DuckDBMatching(const char *sdata, size_t slen, const char *pdata, size_t plen) {
  size_t pidx = 0;
  size_t sidx = 0;
  for (; pidx < plen && sidx < slen;) {
    auto pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
    auto schar = umbra::Utf8::readCodePoint(&sdata[sidx], sdata + slen);

    if (pchar.codePoint == Tokenizer::UNDERSCORE) {
      pidx = pchar.next - pdata;
      sidx = schar.next - sdata;
    } else if (pchar.codePoint == Tokenizer::PERCENTAGE) {
      while (pidx < plen && pchar.codePoint == Tokenizer::PERCENTAGE) {
        pidx  = pchar.next - pdata;
        pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
      }
      if (pidx == plen) { return true; /* tail is acceptable */ }
      for (; sidx < slen;) {
        if (DuckDBMatching(sdata + sidx, slen - sidx, pdata + pidx, plen - pidx)) { return true; }
        sidx  = schar.next - sdata;
        schar = umbra::Utf8::readCodePoint(&sdata[sidx], sdata + slen);
      }
      return false;
    } else if (pchar.codePoint == schar.codePoint) {
      pidx = pchar.next - pdata;
      sidx = schar.next - sdata;
    } else {
      return false;
    }
  }
  auto pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
  while (pidx < plen && pchar.codePoint == Tokenizer::PERCENTAGE) {
    pidx  = pchar.next - pdata;
    pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
  }
  return pidx == plen && sidx == slen;
}

bool GreedyMatching(const char *sdata, size_t slen, const char *pdata, size_t plen) {
  size_t sidx = 0;
  size_t pidx = 0;

  // backtracking positions (byte offsets)
  size_t star_pidx = (size_t)-1;
  size_t star_sidx = (size_t)-1;

  while (sidx < slen) {
    if (pidx < plen) {
      auto pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);

      // Case 1: '_' matches exactly one Unicode codepoint
      if (pchar.codePoint == Tokenizer::UNDERSCORE) {
        auto schar = umbra::Utf8::readCodePoint(&sdata[sidx], sdata + slen);
        sidx       = schar.next - sdata;
        pidx       = pchar.next - pdata;
        continue;
      }

      // Case 2: exact Unicode codepoint match
      if (pchar.codePoint != Tokenizer::PERCENTAGE) {
        auto schar = umbra::Utf8::readCodePoint(&sdata[sidx], sdata + slen);
        if (pchar.codePoint == schar.codePoint) {
          sidx = schar.next - sdata;
          pidx = pchar.next - pdata;
          continue;
        }
      }

      // Case 3: '%' wildcard
      if (pchar.codePoint == Tokenizer::PERCENTAGE) {
        star_pidx = pchar.next - pdata;  // pattern after %
        star_sidx = sidx;
        pidx      = star_pidx;
        continue;
      }
    }

    // Case 4: mismatch -- backtrack to last '%'
    if (star_pidx != (size_t)-1) {
      // consume one more Unicode codepoint from string
      auto schar = umbra::Utf8::readCodePoint(&sdata[star_sidx], sdata + slen);
      star_sidx  = schar.next - sdata;

      sidx = star_sidx;
      pidx = star_pidx;
      continue;
    }

    return false;
  }

  // Consume remaining '%' in pattern
  while (pidx < plen) {
    auto pchar = umbra::Utf8::readCodePoint(&pdata[pidx], pdata + plen);
    if (pchar.codePoint != Tokenizer::PERCENTAGE) break;
    pidx = pchar.next - pdata;
  }

  return pidx == plen;
}

template <typename StringT>
auto AhoCorasickMultiplePatterns(const char *s, size_t slen, std::vector<StringT> patterns, std::string_view escape_str)
  -> std::vector<bool> {
  // #1. Aho-Corasick env. Trie must be global scope
  auto trie = aho_corasick::AhoCorasick();
  auto t    = trie.Local();

  // #2. Pre-processing the pattern into pattern skeleton.
  // With morsel-driven processing, we can split the pattern table into multiple morsels,
  //   then build the associated pattern skeleton.
  // Afterwards, combining all those skeletons into a single global `build_side`.
  // For prototyping, just a single constructor for both steps.
  auto build_side = aho_corasick::PatternAnalyzer(patterns, escape_str, trie);

  // #3. All morsels must be completed until here.
  // Building suffix & output links of the global AhoCorasick automaton
  trie.BuildSuffixLink(1);

  // #4. Start from now on, per-text matching
  // Result bitmap declared
  std::vector<bool> result(patterns.size(), false);

  // #5. Early filtering those patterns
  for (auto idx = 0U; idx < patterns.size(); idx++) {
    const auto &sket = build_side.GetSkeleton(idx);
    // Special pattern: only containing wildcard characters
    if (sket.IsEmpty() || sket.OnlyWildcard()) {
      auto &pat   = patterns[idx];
      result[idx] = aho_corasick::Skeleton::SpecialMatchEmptyPattern(reinterpret_cast<char *>(pat.data()),
                                                                     static_cast<u32>(pat.size()), s, slen);
    }
  }

  // #6. Matching rows with the AhoCorasick automaton
  auto iterator = aho_corasick::TextParserIterator(s, slen, &build_side, &trie);
  while (iterator.CanAdvanceOneCodePoint()) {
    // #6.1. Check the possible matched literals
    iterator.IterateOneCodePoint(result);

    // #6.2. Check the queue to proceed with the delayed matching
    iterator.ProcessDelayedMatching();
  }

  return result;
}