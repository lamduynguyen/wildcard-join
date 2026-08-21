#pragma once

// The probe side of a LIKE join, written once.
//
// Driving the automaton over a row is four lines and every caller writes the
// same four, except that the wildcard-only short circuit lives in the caller
// rather than in the iterator, so a caller that forgets it gets right answers
// on every workload without a `%` or `%%` pattern and wrong ones on the first
// workload that has one. Two callers shipped with that bug.
// Anything checking answers against a stored digest has to be on the right
// side of it, so the correct version is here and callers include it.
//
// benchmark/probe.h is the same short circuit for the benchmark side. Two
// files rather than one because this one includes matcher.h and compares
// against the oracle row by row, which a timed loop must not do.

#include "matcher.h"

#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/parser.h"
#include "aho_corasick/skeleton.h"
#include "common/typedef.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace probe {

using aho_corasick::AhoCorasick;
using aho_corasick::PatternAnalyzer;
using aho_corasick::TextParserIterator;

// Owns a compiled pattern set so several probe strategies can share it.
struct BuildSide {
  AhoCorasick trie;
  std::vector<std::string> patterns;
  std::unique_ptr<PatternAnalyzer> analyzer;

  explicit BuildSide(std::vector<std::string> pats) : patterns(std::move(pats)) {
    analyzer = std::make_unique<PatternAnalyzer>(patterns, "", trie);
    trie.BuildSuffixLink();
  }
};

// The wildcard-only short circuit lives in the caller in every existing probe
// loop, so it lives here too rather than inside the iterator.
inline void ApplyWildcardOnly(BuildSide &b, const std::string &text, std::vector<bool> &result) {
  for (auto idx = 0U; idx < b.patterns.size(); idx++) {
    const auto &sket = b.analyzer->GetSkeleton(idx);
    if (sket.IsEmpty() || sket.OnlyWildcard()) {
      result[idx] = aho_corasick::Skeleton::SpecialMatchEmptyPattern(
        b.patterns[idx].data(), static_cast<u32>(b.patterns[idx].size()), text.data(), text.size());
    }
  }
}

// One row through an iterator the caller owns and reuses. This is the shape
// the benchmark times, because building an iterator per row allocates an LRU
// cache per pattern and that allocation is the whole per-row cost once M is
// large.
inline void Drive(BuildSide &b, TextParserIterator &iter, const std::string &text, std::vector<bool> &result) {
  std::fill(result.begin(), result.end(), false);
  ApplyWildcardOnly(b, text, result);
  iter.ResetText(text.data(), text.size());
  while (iter.CanAdvanceOneCodePoint()) {
    iter.IterateOneCodePoint(result);
    iter.ProcessDelayedMatching();
  }
}

// One row through an iterator built for it and thrown away, which is the
// behaviour ResetText replaced. Kept because it is the control test_reuse
// compares against.
inline auto ProbeFresh(BuildSide &b, const std::string &text) -> std::vector<bool> {
  std::vector<bool> result(b.patterns.size(), false);
  ApplyWildcardOnly(b, text, result);
  TextParserIterator iter(text.data(), text.size(), b.analyzer.get(), &b.trie);
  while (iter.CanAdvanceOneCodePoint()) {
    iter.IterateOneCodePoint(result);
    iter.ProcessDelayedMatching();
  }
  return result;
}

// The oracle, one row against every pattern. Slower than the automaton by the
// factor the benchmarks exist to measure, which is why it is only used where
// correctness is the output and time is not.
inline auto Reference(const std::vector<std::string> &patterns, const std::string &text) -> std::vector<bool> {
  std::vector<bool> out(patterns.size(), false);
  for (auto idx = 0U; idx < patterns.size(); idx++) {
    out[idx] = NljRecursiveMatch(text.data(), text.size(), patterns[idx].data(), patterns[idx].size());
  }
  return out;
}

}  // namespace probe
