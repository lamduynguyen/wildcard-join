#pragma once

// The Aho-Corasick side of a benchmark run: build the automaton once, then
// probe one row at a time with an iterator that outlives the row.
//
// It lives in a header because two programs have to do this identically.
// `ac_bench` times it against the recursive matcher and against DuckDB,
// `perf_guard` times it against itself at two pattern counts and two row
// shapes. A guard that measured a slightly different probe from the one the
// benchmark reports would be guarding nothing.
//
// `test/probe.h` is the same shape and is deliberately still its own file. It
// carries the fresh-iterator variant the reuse test needs and the row by row
// comparison against the oracle, neither of which belongs in a benchmark. The
// oracle itself is shared, in benchmark/reference.h.

#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/parser.h"
#include "aho_corasick/skeleton.h"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bench {

using bench_clock = std::chrono::steady_clock;

// Materialises the build side once, reusable across runs.
//
// `patterns` points at the caller's vector rather than copying it. The
// PatternAnalyzer already requires that vector to outlive it, so this borrows
// nothing that was not borrowed already, and the probe needs the pattern text
// for the wildcard-only case below.
struct AcBuild {
  std::unique_ptr<aho_corasick::AhoCorasick> trie;
  std::unique_ptr<aho_corasick::PatternAnalyzer> build_side;
  const std::vector<std::string> *patterns = nullptr;
  // Indices of the patterns with no literal, worked out once. Which patterns
  // those are is a property of the build side and not of the row, so asking
  // every skeleton about it on every row is M work per row for an answer that
  // cannot have changed. On short_text_M=1k that walk was 50 million skeleton
  // lookups to find the same empty set 50000 times.
  std::vector<size_t> wildcard_only;
  std::chrono::nanoseconds build_ns{0};
};

// `escape` is the SQL ESCAPE clause, empty for none. The synthetic workloads
// never set it; the corpora emitted by hackernews-processing carry it in their
// meta.json, so the probe has to take it rather than assume LIKE.
inline auto BuildAc(std::vector<std::string> &patterns, std::string_view escape = {}) -> AcBuild {
  AcBuild b;
  b.trie       = std::make_unique<aho_corasick::AhoCorasick>();
  auto t0      = bench_clock::now();
  b.build_side = std::make_unique<aho_corasick::PatternAnalyzer>(patterns, escape, *b.trie);
  b.trie->BuildSuffixLink();
  b.build_ns = bench_clock::now() - t0;
  b.patterns = &patterns;

  // Outside the clock. It is O(M) once and the build it would be attributed to
  // is already O(M), but it is bookkeeping for this harness rather than work
  // the automaton needs, so it does not belong in a reported build time.
  for (size_t i = 0; i < patterns.size(); i++) {
    const auto &sk = b.build_side->GetSkeleton(i);
    if (sk.IsEmpty() || sk.OnlyWildcard()) { b.wildcard_only.push_back(i); }
  }
  return b;
}

// Drive TextParserIterator over a single text, mirroring the per-row logic in
// test/matcher.h::AhoCorasickMultiplePatterns.
//
// The iterator is owned by the caller and reused across rows. Constructing one
// per row allocates an LRU cache per pattern, which is the whole per-row cost
// once M gets large.
inline auto AcProbe(const AcBuild &b, aho_corasick::TextParserIterator &iter, const std::string &text,
                    std::vector<bool> &result) -> void {
  std::fill(result.begin(), result.end(), false);

  // Wildcard-only patterns have no literal for the automaton to be in a state
  // about, so they are answered here rather than by the iterator.
  // Empty for every workload except wildcards_M=100.
  for (size_t i : b.wildcard_only) {
    const auto &pat = (*b.patterns)[i];
    result[i] = aho_corasick::Skeleton::SpecialMatchEmptyPattern(pat.data(), static_cast<u32>(pat.size()), text.data(),
                                                                 text.size());
  }

  iter.ResetText(text.data(), text.size());
  while (iter.CanAdvanceOneCodePoint()) {
    iter.IterateOneCodePoint(result);
    iter.ProcessDelayedMatching();
  }
}

}  // namespace bench
