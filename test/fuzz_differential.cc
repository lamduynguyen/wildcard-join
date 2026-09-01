// Differential fuzzer: the Aho-Corasick probe against the recursive reference.
//
// Everything else in test/ is a case somebody thought of. The cases that have
// actually been wrong in this repo were not thought of: a delayed underscore
// landing on a multi-byte code point, a matcher left parked on a later segment
// when the previous row ran out, an LRU cache reallocating under the iterator.
// Those are found by running a lot of random input through two implementations
// and comparing, which is what this does.
//
// It is not libFuzzer. libFuzzer needs clang and -fsanitize=fuzzer, and this
// has to run under gcc, under apple clang, and under the ASan and UBSan
// presets, which is most of what the coverage guided search would have bought.
// What is here is a plain loop over a seeded generator with a wall clock
// budget, which is reproducible from the seed it prints, and which composes
// with the sanitizers instead of competing with them.
//
//   fuzz_differential                 run for the default budget from a fixed seed
//   fuzz_differential --seconds 60    what CI runs
//   fuzz_differential --seed 12345    replay the seed a failure printed
//
// Exits 0 if everything agreed, 1 on the first disagreement, having printed
// the pattern, the text, and both as hex.

#include "matcher.h"

#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/parser.h"
#include "aho_corasick/skeleton.h"
#include "fmt/format.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

using aho_corasick::AhoCorasick;
using aho_corasick::PatternAnalyzer;
using aho_corasick::TextParserIterator;

// A round's alphabet. Multi-byte entries are the point of having this at all:
// the delayed underscore queue exists because one code point of pattern can
// consume a different number of bytes than one code point of text, and an
// alphabet of ASCII letters never exercises it.
struct Alphabet {
  const char *name;
  std::vector<std::string> chars;
};

auto MakeAlphabets() -> std::vector<Alphabet> {
  return {
    // Two letters. Short strings over a tiny alphabet collide constantly, so
    // suffix links and output links get walked instead of the automaton
    // falling back to the root on nearly every code point.
    {"ascii2", {"a", "b"}},
    {"ascii4", {"a", "b", "c", "d"}},
    // Two byte code points, so a '_' in the pattern advances the text by two
    // bytes and the pattern by one.
    {"latin1sup", {"é", "ü", "ñ"}},
    // Three byte.
    {"cjk", {"中", "文", "あ"}},
    // Four byte, which is where a code point does not fit in a 16 bit unit and
    // where a naive length assumption tends to be off by two rather than one.
    {"emoji", {"\U0001f600", "\U0001f680"}},
    // The interesting one. Widths change from code point to code point inside
    // a single string, so every offset in the delayed queue is a different
    // distance from the last.
    {"mixed", {"a", "b", "é", "中", "\U0001f600"}},
  };
}

auto Pick(std::mt19937_64 &rng, size_t n) -> size_t { return std::uniform_int_distribution<size_t>(0, n - 1)(rng); }

auto GenText(std::mt19937_64 &rng, const Alphabet &a, size_t max_cp) -> std::string {
  std::string out;
  auto len = Pick(rng, max_cp + 1);
  for (size_t i = 0; i < len; i++) { out += a.chars[Pick(rng, a.chars.size())]; }
  return out;
}

// Weights matter more than they look. Too many percent signs and every pattern
// matches, so a false positive in the probe is invisible. Too few and every
// pattern is a literal, which is the easy case. Roughly one wildcard in four
// keeps the hit rate somewhere in the middle for the string lengths below.
auto GenPattern(std::mt19937_64 &rng, const Alphabet &a, size_t max_cp) -> std::string {
  std::string out;
  auto len = Pick(rng, max_cp + 1);
  for (size_t i = 0; i < len; i++) {
    auto roll = Pick(rng, 8);
    if (roll == 0) {
      out += '%';
    } else if (roll == 1) {
      out += '_';
    } else {
      out += a.chars[Pick(rng, a.chars.size())];
    }
  }
  return out;
}

auto Hex(const std::string &s) -> std::string {
  std::string out;
  for (unsigned char c : s) { out += fmt::format("{:02x} ", c); }
  return out;
}

// Same shape as the probe loop in the benchmark and in test_reuse: one
// analyzer, one iterator, reused across every text in the round. Reuse is what
// the production path does and it is the part most likely to be wrong.
struct BuildSide {
  AhoCorasick trie;
  std::vector<std::string> patterns;
  std::unique_ptr<PatternAnalyzer> analyzer;

  explicit BuildSide(std::vector<std::string> pats) : patterns(std::move(pats)) {
    analyzer = std::make_unique<PatternAnalyzer>(patterns, "", trie);
    trie.BuildSuffixLink();
  }
};

void ApplyWildcardOnly(BuildSide &b, const std::string &text, std::vector<bool> &result) {
  for (auto idx = 0U; idx < b.patterns.size(); idx++) {
    const auto &sket = b.analyzer->GetSkeleton(idx);
    if (sket.IsEmpty() || sket.OnlyWildcard()) {
      result[idx] = aho_corasick::Skeleton::SpecialMatchEmptyPattern(
        b.patterns[idx].data(), static_cast<u32>(b.patterns[idx].size()), text.data(), text.size());
    }
  }
}

void Drive(BuildSide &b, const std::string &text, std::vector<bool> &result) {
  std::fill(result.begin(), result.end(), false);
  ApplyWildcardOnly(b, text, result);
  TextParserIterator iter(text.data(), text.size(), b.analyzer.get(), &b.trie);
  while (iter.CanAdvanceOneCodePoint()) {
    iter.IterateOneCodePoint(result);
    iter.ProcessDelayedMatching();
  }
}

struct Counters {
  uint64_t rounds     = 0;
  uint64_t decisions  = 0;  // (text, pattern) pairs compared
  uint64_t agreed_yes = 0;  // pairs both sides called a match
};

// Prints everything needed to turn a failure into a test case, then leaves.
// Stopping on the first one is deliberate: a bug in the probe usually fires on
// thousands of inputs, and the second thousand tell you nothing the first one
// did not.
[[noreturn]] void Fail(uint64_t seed, const Counters &c, const Alphabet &a, const std::string &pattern,
                       const std::string &text, bool ac, bool ref) {
  fmt::print(stderr, "\ndisagreement after {} rounds and {} decisions\n", c.rounds, c.decisions);
  fmt::print(stderr, "  replay with: fuzz_differential --seed {}\n", seed);
  fmt::print(stderr, "  alphabet: {}\n", a.name);
  fmt::print(stderr, "  pattern:  '{}'\n", pattern);
  fmt::print(stderr, "  hex:      {}\n", Hex(pattern));
  fmt::print(stderr, "  text:     '{}'\n", text);
  fmt::print(stderr, "  hex:      {}\n", Hex(text));
  fmt::print(stderr, "  aho-corasick says {}, recursive reference says {}\n", ac, ref);
  // The fuzzer is single threaded, and this is the path where it has already
  // found a disagreement and is giving up.
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  std::exit(1);
}

}  // namespace

auto main(int argc, char **argv) -> int {
  // Two and a half seconds by default, which is what ctest runs. CI passes
  // --seconds 60. The default seed is fixed so a plain run is reproducible;
  // the point of the budget is depth, not a different sample every time.
  double seconds  = 2.5;
  uint64_t seed   = 0x5eed1234;
  bool seed_given = false;

  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
      // strtod and not atof, which reports no error at all: a typo in the
      // budget would silently become zero seconds and the job would pass in
      // no time looking like it had run.
      char *end = nullptr;
      seconds   = std::strtod(argv[++i], &end);
      if (end == argv[i] || *end != '\0' || seconds <= 0) {
        fmt::print(stderr, "--seconds wants a positive number, got '{}'\n", argv[i]);
        return 2;
      }
    } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      seed       = std::strtoull(argv[++i], nullptr, 0);
      seed_given = true;
    } else {
      fmt::print(stderr, "usage: {} [--seconds N] [--seed N]\n", argv[0]);
      return 2;
    }
  }

  auto alphabets = MakeAlphabets();
  Counters c;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);

  fmt::print("fuzzing for {} s from seed {:#x}{}\n", seconds, seed, seed_given ? " (replay)" : "");

  // The seed advances per round, and the round seed is what gets printed on a
  // failure, so a replay reproduces that round without replaying every round
  // before it.
  for (uint64_t round = 0;; round++) {
    if ((round & 0x3f) == 0 && std::chrono::steady_clock::now() >= deadline) { break; }

    auto round_seed = seed + round;
    std::mt19937_64 rng(round_seed);

    const auto &a = alphabets[Pick(rng, alphabets.size())];

    // Small. A disagreement on a four code point string is something you can
    // read; the same disagreement on a two hundred byte string is a bisection
    // job. Long strings are covered by the benchmark verification in
    // baseline_bench, which runs the same comparison over real sized rows.
    auto max_pattern_cp = 1 + Pick(rng, 8);
    auto max_text_cp    = 1 + Pick(rng, 12);
    auto n_patterns     = 1 + Pick(rng, 8);
    auto n_texts        = 1 + Pick(rng, 8);

    std::vector<std::string> patterns;
    for (size_t i = 0; i < n_patterns; i++) { patterns.push_back(GenPattern(rng, a, max_pattern_cp)); }

    std::vector<std::string> texts;
    for (size_t i = 0; i < n_texts; i++) { texts.push_back(GenText(rng, a, max_text_cp)); }

    BuildSide b(patterns);
    std::vector<bool> result(patterns.size(), false);

    // Forward, then the same texts again, then backwards. Three passes over
    // one build, because an order dependent bug is one you only see when a
    // row runs after a different row.
    for (int pass = 0; pass < 3; pass++) {
      for (size_t ti = 0; ti < texts.size(); ti++) {
        const auto &text = texts[pass == 2 ? texts.size() - 1 - ti : ti];
        Drive(b, text, result);
        for (size_t pi = 0; pi < patterns.size(); pi++) {
          const bool ref = NljRecursiveMatch(text.data(), text.size(), patterns[pi].data(), patterns[pi].size());
          c.decisions++;
          if (result[pi] != ref) { Fail(round_seed, c, a, patterns[pi], text, static_cast<bool>(result[pi]), ref); }
          if (ref) { c.agreed_yes++; }
        }
      }
    }
    c.rounds++;
  }

  // The match rate is printed because it is the one number that says whether
  // this run was worth anything. A run where nothing ever matched would report
  // millions of agreeing decisions and have tested only the reject path.
  auto rate = c.decisions == 0 ? 0.0 : 100.0 * static_cast<double>(c.agreed_yes) / static_cast<double>(c.decisions);
  fmt::print("no disagreements: {} rounds, {} decisions, {:.1f}% of them matches\n", c.rounds, c.decisions, rate);
  return 0;
}
