#pragma once

// The synthetic workload the baseline benchmark runs on, in a header because
// two things need to agree on it byte for byte: the benchmark that times the
// probe, and the test that checks the answers against the digests stored in
// reproducibility/expected. A copy in each would be a copy that drifts, and
// the whole point of a stored digest is that nothing drifts silently.
//
// Everything here is a pure function of the seed. See the note on Pick below
// for why that sentence took a fix to become true.

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace workload {

struct Workload {
  std::string name;
  std::vector<std::string> texts;     // probe side
  std::vector<std::string> patterns;  // build side
  size_t total_text_bytes = 0;
  uint64_t digest         = 0;
};

// The standard library's distributions are not portable. The engine is: a
// std::mt19937 seeded with 0xC0FFEE produces the same sequence everywhere,
// that part is specified down to the constants. What is not specified is how
// std::uniform_int_distribution and std::bernoulli_distribution turn that
// sequence into a value in a range, and libstdc++ and libc++ pick different
// algorithms, so the same seed built a different corpus on every machine we
// ran on. Every benchmark number was for a workload only that machine had.
//
// Same decision as test/corpus.h, and the copy here is deliberate: the golden
// file under reproducibility/expected is a statement about this header, so it
// generates its corpus from code that lives next to the specs rather than from
// a header in test/.
//
// Plain modulo, so it is biased. For n = 26 the low 22 values are reachable
// by one extra multiple of 26 out of 2^32, which is a difference of about
// 1e-8 in their probability. These are filler words in a benchmark corpus.
inline auto Pick(std::mt19937 &rng, size_t n) -> size_t { return static_cast<size_t>(rng() % n); }

// Bernoulli as a comparison against a threshold in the engine's own output
// range. One draw, and the only floating point is computing the threshold.
inline auto Chance(std::mt19937 &rng, double p) -> bool {
  const auto threshold = static_cast<uint64_t>(p * (static_cast<double>(std::mt19937::max()) + 1.0));
  return static_cast<uint64_t>(rng()) < threshold;
}

inline constexpr uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
inline constexpr uint64_t FNV_PRIME  = 0x100000001b3ULL;

// FNV-1a over the corpus, so "did those two runs use the same data" is a
// question you can answer from a log rather than from a rebuild. No security
// property wanted or claimed, it just has to be cheap and reimplementable in
// three lines by anybody checking us.
inline void DigestUpdate(uint64_t &h, std::string_view s) {
  for (unsigned char c : s) {
    h ^= c;
    h *= FNV_PRIME;
  }
  // Length delimiter, so ["ab", "c"] and ["a", "bc"] do not collide.
  h ^= '\n';
  h *= FNV_PRIME;
}

// Deterministic English-ish word generator -- ASCII so the AC trie depth
// matches code-point depth (no Unicode multi-byte). Lets us measure raw
// algorithmic cost without UTF-8 decode noise.
inline auto RngWord(std::mt19937 &rng, size_t min_len, size_t max_len) -> std::string {
  std::string s(min_len + Pick(rng, max_len - min_len + 1), 0);
  for (auto &c : s) c = static_cast<char>('a' + Pick(rng, 26));
  return s;
}

// Build a text by concatenating `n_words` random words with spaces, then
// splicing in one of the literal tokens with probability `hit_prob`.
inline auto BuildText(std::mt19937 &rng, size_t n_words, const std::vector<std::string> &tokens,
                      double hit_prob) -> std::string {
  std::string out;
  out.reserve(n_words * 8);

  bool injected = false;
  for (size_t i = 0; i < n_words; i++) {
    if (!injected && Chance(rng, hit_prob) && !tokens.empty()) {
      out.append(tokens[Pick(rng, tokens.size())]);
      injected = true;
    } else {
      out.append(RngWord(rng, 3, 9));
    }
    if (i + 1 < n_words) out.push_back(' ');
  }
  return out;
}

// One row of the workload table. Six size_t and a double in a row is exactly
// what bugprone-easily-swappable-parameters complains about in MakeWorkload,
// and passing this struct instead is the fix that check wants. That is a
// separate change: the free function keeps its signature here so this header
// is a move and not a redesign.
struct Spec {
  std::string_view name;
  size_t n_texts;
  size_t n_patterns;
  size_t words_per_text;
  size_t literal_min;
  size_t literal_max;
  double hit_prob;
  // Patterns made only of wildcards, appended after the generated ones. They
  // do not go through the random generator, so a spec that asks for none draws
  // exactly the same numbers as it did before this field existed.
  size_t n_wildcard_only = 0;
};

// The wildcard-only patterns, in the order they get appended. Four rather than
// one because they are answered by two different branches: `%` and `%%` have
// no code points to match at all, `_` and `__` have a length to check.
inline constexpr std::array<std::string_view, 4> WILDCARD_ONLY = {"%", "%%", "_", "__"};

// The seed. One place, because the benchmark and the golden test have to use
// the same one and a literal in two files is a literal that drifts.
inline constexpr uint32_t SEED = 0xC0FFEEu;

// Workload generator. Each generated pattern is `%TOKEN%`, single segment, one
// literal -- the regime where AC's suffix links should pay off cleanly. The hit
// rate (fraction of texts whose `result` bitmap will have at least one true
// entry) is roughly `hit_prob`, not counting the wildcard-only patterns, which
// match nearly everything by construction.
inline auto MakeWorkload(const Spec &s, uint32_t seed = SEED) -> Workload {
  std::mt19937 rng(seed);

  std::vector<std::string> tokens;
  tokens.reserve(s.n_patterns);
  for (size_t i = 0; i < s.n_patterns; i++) tokens.push_back(RngWord(rng, s.literal_min, s.literal_max));

  std::vector<std::string> patterns;
  patterns.reserve(s.n_patterns + s.n_wildcard_only);
  for (const auto &t : tokens) patterns.push_back("%" + t + "%");
  for (size_t i = 0; i < s.n_wildcard_only; i++) patterns.emplace_back(WILDCARD_ONLY[i % WILDCARD_ONLY.size()]);

  std::vector<std::string> texts;
  texts.reserve(s.n_texts);
  size_t total = 0;
  for (size_t i = 0; i < s.n_texts; i++) {
    texts.push_back(BuildText(rng, s.words_per_text, tokens, s.hit_prob));
    total += texts.back().size();
  }

  // Both sides, in generation order, so the digest covers everything the
  // benchmark then times.
  uint64_t digest = FNV_OFFSET;
  for (const auto &p : patterns) DigestUpdate(digest, p);
  for (const auto &t : texts) DigestUpdate(digest, t);

  return Workload{std::string(s.name), std::move(texts), std::move(patterns), total, digest};
}

// The workloads. Changing any field here invalidates the matching row in
// reproducibility/expected/baseline_workloads.tsv, which is the intent: the
// golden test fails and the file has to be regenerated deliberately.
inline constexpr std::array<Spec, 7> BASELINE_SPECS = {{
  {"sweep_M=10", 10000, 10, 50, 4, 8, 0.05},
  {"sweep_M=100", 10000, 100, 50, 4, 8, 0.05},
  {"sweep_M=1k", 10000, 1000, 50, 4, 8, 0.05},
  {"sweep_M=10k", 2000, 10000, 50, 4, 8, 0.05},
  {"short_text_M=1k", 50000, 1000, 10, 4, 8, 0.10},
  {"long_text_M=1k", 1000, 1000, 500, 4, 8, 0.05},
  // The one with `%`, `%%`, `_` and `__` in it. Every other spec here is
  // patterns with exactly one literal, which is the regime the automaton is
  // good at and also the regime where a probe that quietly drops the
  // wildcard-only case still agrees with everything it is checked against.
  // `%` matches all 2000 rows, so a probe that misses it is off by
  // 2000 pairs and not by a rounding error.
  {"wildcards_M=100", 2000, 100, 50, 4, 8, 0.05, 4},
}};

}  // namespace workload
