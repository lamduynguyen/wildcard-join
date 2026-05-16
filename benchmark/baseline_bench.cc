// Baseline benchmark: prototype-string Aho-Corasick LIKE join vs the
// recursive matcher used by DuckDB's `TemplatedLikeOperator`
// (src/function/scalar/string/like.cpp).
//
// What this measures:
//   * Build time     -- compile M patterns + ART trie + suffix links.
//   * AC probe time  -- per text row, run TextParserIterator over the M patterns.
//   * Baseline time  -- per text row, loop over M patterns and call DuckDBMatching.
//
// Output is one CSV-ish line per (workload, implementation) plus a small
// summary table at the end.

#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/parser.h"
#include "aho_corasick/skeleton.h"
#include "common/utf8.h"
#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#ifdef HN_BENCH_HAVE_DUCKDB
#include <duckdb.h>
#endif

using aho_corasick::AhoCorasick;
using aho_corasick::PatternAnalyzer;
using aho_corasick::Skeleton;
using aho_corasick::TextParserIterator;
using aho_corasick::Tokenizer;
using namespace std::string_view_literals;
using bench_clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Reference matchers (lifted verbatim from test/matcher.h so the benchmark
// stands alone and does not pull in gtest).

static bool DuckDBMatching(const char *sdata, size_t slen, const char *pdata, size_t plen) {
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
      if (pidx == plen) { return true; }
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

// ---------------------------------------------------------------------------
// Workload generators

struct Workload {
  std::string                name;
  std::vector<std::string>   texts;     // probe side
  std::vector<std::string>   patterns;  // build side
  size_t                     total_text_bytes = 0;
};

namespace {

// Deterministic English-ish word generator -- ASCII so the AC trie depth
// matches code-point depth (no Unicode multi-byte). Lets us measure raw
// algorithmic cost without UTF-8 decode noise.
auto rng_word(std::mt19937 &rng, size_t min_len, size_t max_len) -> std::string {
  std::uniform_int_distribution<size_t> len_dist(min_len, max_len);
  std::uniform_int_distribution<int>    chr_dist('a', 'z');
  std::string s(len_dist(rng), 0);
  for (auto &c : s) c = static_cast<char>(chr_dist(rng));
  return s;
}

// Build a text by concatenating `n_words` random words with spaces, then
// splicing in one of the literal tokens with probability `hit_prob`.
auto build_text(std::mt19937 &rng, size_t n_words, const std::vector<std::string> &tokens, double hit_prob)
    -> std::string {
  std::string out;
  out.reserve(n_words * 8);
  std::bernoulli_distribution                hit(hit_prob);
  std::uniform_int_distribution<size_t>      tok(0, tokens.size() - 1);

  bool injected = false;
  for (size_t i = 0; i < n_words; i++) {
    if (!injected && hit(rng) && !tokens.empty()) {
      out.append(tokens[tok(rng)]);
      injected = true;
    } else {
      out.append(rng_word(rng, 3, 9));
    }
    if (i + 1 < n_words) out.push_back(' ');
  }
  return out;
}

// Workload generator. Each pattern is `%TOKEN%`, single segment, one literal --
// the regime where AC's suffix links should pay off cleanly. The hit rate
// (fraction of texts whose `result` bitmap will have at least one true entry)
// is roughly `hit_prob`.
auto make_workload(const std::string &name, size_t n_texts, size_t n_patterns,
                   size_t words_per_text, size_t literal_min, size_t literal_max,
                   double hit_prob, uint32_t seed) -> Workload {
  std::mt19937 rng(seed);

  std::vector<std::string> tokens;
  tokens.reserve(n_patterns);
  for (size_t i = 0; i < n_patterns; i++) tokens.push_back(rng_word(rng, literal_min, literal_max));

  std::vector<std::string> patterns;
  patterns.reserve(n_patterns);
  for (const auto &t : tokens) patterns.push_back("%" + t + "%");

  std::vector<std::string> texts;
  texts.reserve(n_texts);
  size_t total = 0;
  for (size_t i = 0; i < n_texts; i++) {
    texts.push_back(build_text(rng, words_per_text, tokens, hit_prob));
    total += texts.back().size();
  }

  return Workload{name, std::move(texts), std::move(patterns), total};
}

// AC build helper -- materialises the build side once, reusable across runs.
struct AcBuild {
  std::unique_ptr<AhoCorasick>     trie;
  std::unique_ptr<PatternAnalyzer> build_side;
  std::chrono::nanoseconds         build_ns{0};
};

auto build_ac(std::vector<std::string> &patterns) -> AcBuild {
  AcBuild b;
  b.trie       = std::make_unique<AhoCorasick>();
  auto t0      = bench_clock::now();
  b.build_side = std::make_unique<PatternAnalyzer>(patterns, ""sv, *b.trie);
  b.trie->BuildSuffixLink();
  b.build_ns = bench_clock::now() - t0;
  return b;
}

// AC probe: drive TextParserIterator over a single text, mirroring the
// per-row logic in test/matcher.h::AhoCorasickMultiplePatterns.
auto ac_probe(const AcBuild &b, const std::string &text, std::vector<bool> &result) -> void {
  std::fill(result.begin(), result.end(), false);

  // Wildcard-only patterns short-circuit; for "%TOKEN%" workloads this is empty.
  for (size_t i = 0; i < b.build_side->Size(); i++) {
    const auto &sk = b.build_side->GetSkeleton(i);
    if (sk.IsEmpty() || sk.OnlyWildcard()) {
      auto &pat   = const_cast<std::string &>(text);  // unused branch in this workload
      (void)pat;
    }
  }

  TextParserIterator iter(text.data(), text.size(), b.build_side.get(), b.trie.get());
  while (iter.CanAdvanceOneCodePoint()) {
    iter.IterateOneCodePoint(result);
    iter.ProcessDelayedMatching();
  }
}

// Baseline probe: loop over every pattern, call DuckDBMatching.
auto duckdb_probe(const std::vector<std::string> &patterns, const std::string &text, std::vector<bool> &result)
    -> void {
  for (size_t i = 0; i < patterns.size(); i++) {
    result[i] = DuckDBMatching(text.data(), text.size(), patterns[i].data(), patterns[i].size());
  }
}

struct RunStats {
  std::chrono::nanoseconds build_ns{0};
  std::chrono::nanoseconds probe_ns{0};
  size_t                   matches = 0;
  size_t                   bytes   = 0;
  size_t                   rows    = 0;
};

auto run_ac(Workload &w, int repeats) -> RunStats {
  RunStats         best{};
  best.probe_ns = std::chrono::nanoseconds::max();

  for (int r = 0; r < repeats; r++) {
    auto b      = build_ac(w.patterns);
    auto result = std::vector<bool>(w.patterns.size(), false);

    size_t matches = 0;
    auto   t0      = bench_clock::now();
    for (const auto &t : w.texts) {
      ac_probe(b, t, result);
      for (auto v : result) matches += v ? 1 : 0;
    }
    auto probe = bench_clock::now() - t0;
    if (probe < best.probe_ns) {
      best.probe_ns = probe;
      best.build_ns = b.build_ns;
      best.matches  = matches;
      best.bytes    = w.total_text_bytes;
      best.rows     = w.texts.size();
    }
  }
  return best;
}

#ifdef HN_BENCH_HAVE_DUCKDB
// DuckDB-engine baseline: in-memory DuckDB, bulk-load texts/patterns via the
// C-API Appender, time one COUNT(*) join.
auto run_duckdb_engine(const Workload &w, int repeats) -> RunStats {
  RunStats best{};
  best.probe_ns = std::chrono::nanoseconds::max();
  for (int r = 0; r < repeats; r++) {
    duckdb_database db{};
    duckdb_connection con{};
    if (duckdb_open(nullptr, &db) != DuckDBSuccess) std::exit(3);
    if (duckdb_connect(db, &con) != DuckDBSuccess)  std::exit(3);

    auto build_t0 = bench_clock::now();
    duckdb_result tmp{};
    duckdb_query(con,
                 "CREATE TABLE texts(text_id BIGINT, text VARCHAR);"
                 "CREATE TABLE patterns(pattern_id BIGINT, pattern VARCHAR);",
                 &tmp);
    duckdb_destroy_result(&tmp);

    duckdb_appender at{}, ap{};
    duckdb_appender_create(con, nullptr, "texts",    &at);
    for (size_t i = 0; i < w.texts.size(); ++i) {
      duckdb_append_int64(at, static_cast<int64_t>(i));
      duckdb_append_varchar_length(at, w.texts[i].data(),
                                   static_cast<idx_t>(w.texts[i].size()));
      duckdb_appender_end_row(at);
    }
    duckdb_appender_destroy(&at);

    duckdb_appender_create(con, nullptr, "patterns", &ap);
    for (size_t i = 0; i < w.patterns.size(); ++i) {
      duckdb_append_int64(ap, static_cast<int64_t>(i));
      duckdb_append_varchar_length(ap, w.patterns[i].data(),
                                   static_cast<idx_t>(w.patterns[i].size()));
      duckdb_appender_end_row(ap);
    }
    duckdb_appender_destroy(&ap);
    auto build_ns = bench_clock::now() - build_t0;

    auto probe_t0 = bench_clock::now();
    duckdb_result res{};
    duckdb_query(con,
                 "SELECT COUNT(*) FROM texts t JOIN patterns p ON t.text LIKE p.pattern",
                 &res);
    auto probe_ns = bench_clock::now() - probe_t0;
    auto matches  = static_cast<size_t>(duckdb_value_int64(&res, 0, 0));
    duckdb_destroy_result(&res);
    duckdb_disconnect(&con);
    duckdb_close(&db);

    if (probe_ns < best.probe_ns) {
      best.probe_ns = probe_ns;
      best.build_ns = build_ns;
      best.matches  = matches;
      best.bytes    = w.total_text_bytes;
      best.rows     = w.texts.size();
    }
  }
  return best;
}
#endif

auto run_duckdb(const Workload &w, int repeats) -> RunStats {
  RunStats         best{};
  best.probe_ns = std::chrono::nanoseconds::max();

  for (int r = 0; r < repeats; r++) {
    auto result = std::vector<bool>(w.patterns.size(), false);

    size_t matches = 0;
    auto   t0      = bench_clock::now();
    for (const auto &t : w.texts) {
      duckdb_probe(w.patterns, t, result);
      for (auto v : result) matches += v ? 1 : 0;
    }
    auto probe = bench_clock::now() - t0;
    if (probe < best.probe_ns) {
      best.probe_ns = probe;
      best.matches  = matches;
      best.bytes    = w.total_text_bytes;
      best.rows     = w.texts.size();
    }
  }
  return best;
}

auto ns_to_ms(std::chrono::nanoseconds ns) -> double { return ns.count() / 1.0e6; }
auto throughput_rows_per_s(const RunStats &s) -> double {
  return static_cast<double>(s.rows) / (s.probe_ns.count() / 1.0e9);
}
auto throughput_mb_per_s(const RunStats &s) -> double {
  return (s.bytes / 1.0e6) / (s.probe_ns.count() / 1.0e9);
}

auto print_header() {
  fmt::print("{:<24} {:<8} {:>10} {:>10} {:>10} {:>14} {:>12} {:>12} {:>10}\n",
             "workload", "impl", "rows", "patterns", "MB", "build_ms", "probe_ms", "rows/s", "MB/s");
}

auto print_row(const std::string &name, const std::string &impl, const Workload &w, const RunStats &s) {
  fmt::print("{:<24} {:<8} {:>10} {:>10} {:>10.2f} {:>14.3f} {:>12.3f} {:>12.0f} {:>10.2f}\n",
             name, impl, w.texts.size(), w.patterns.size(),
             w.total_text_bytes / 1.0e6,
             ns_to_ms(s.build_ns), ns_to_ms(s.probe_ns),
             throughput_rows_per_s(s), throughput_mb_per_s(s));
}

}  // namespace

int main() {
  struct Spec {
    std::string name;
    size_t      n_texts;
    size_t      n_patterns;
    size_t      words_per_text;
    size_t      lit_min;
    size_t      lit_max;
    double      hit_prob;
  };

  std::array<Spec, 6> specs = {{
    {"sweep_M=10",      10000,    10,  50, 4, 8, 0.05},
    {"sweep_M=100",     10000,   100,  50, 4, 8, 0.05},
    {"sweep_M=1k",      10000,  1000,  50, 4, 8, 0.05},
    {"sweep_M=10k",      2000, 10000,  50, 4, 8, 0.05},
    {"short_text_M=1k", 50000,  1000,  10, 4, 8, 0.10},
    {"long_text_M=1k",   1000,  1000, 500, 4, 8, 0.05},
  }};

  print_header();
  std::vector<std::pair<std::string, RunStats>> ac_rows, dd_rows, de_rows;
  std::vector<Workload> workloads;
  workloads.reserve(specs.size());

  for (const auto &s : specs) {
    auto w = make_workload(s.name, s.n_texts, s.n_patterns, s.words_per_text, s.lit_min, s.lit_max,
                           s.hit_prob, 0xC0FFEEu);
    fmt::print(stderr, "[{}] building workload + AC...\n", s.name);
    fflush(stderr);
    auto ac = run_ac(w, /*repeats=*/3);
    print_row(s.name, "AC",     w, ac); fflush(stdout);
    fmt::print(stderr, "[{}] DDrec...\n", s.name); fflush(stderr);
    auto dd = run_duckdb(w, /*repeats=*/3);
    print_row(s.name, "DDrec",  w, dd); fflush(stdout);

#ifdef HN_BENCH_HAVE_DUCKDB
    fmt::print(stderr, "[{}] DDeng...\n", s.name); fflush(stderr);
    auto de = run_duckdb_engine(w, /*repeats=*/1);
    print_row(s.name, "DDeng",  w, de); fflush(stdout);
    if (ac.matches != dd.matches || ac.matches != de.matches) {
      fmt::print(stderr, "!! mismatch on {}: AC={}, DDrec={}, DDeng={}\n",
                 s.name, ac.matches, dd.matches, de.matches);
    }
    de_rows.emplace_back(s.name, de);
#else
    if (ac.matches != dd.matches) {
      fmt::print(stderr, "!! mismatch on {}: AC={} matches, DDrec={} matches\n",
                 s.name, ac.matches, dd.matches);
    }
#endif

    workloads.push_back(std::move(w));
    ac_rows.emplace_back(s.name, ac);
    dd_rows.emplace_back(s.name, dd);
  }

  fmt::print("\nSpeedup AC probe vs baselines (probe phase only):\n");
  fmt::print("{:<24} {:>12} {:>12}\n", "workload", "vs_DDrec", "vs_DDeng");
  for (size_t i = 0; i < ac_rows.size(); i++) {
    double rec = static_cast<double>(dd_rows[i].second.probe_ns.count()) /
                 static_cast<double>(ac_rows[i].second.probe_ns.count());
#ifdef HN_BENCH_HAVE_DUCKDB
    double eng = static_cast<double>(de_rows[i].second.probe_ns.count()) /
                 static_cast<double>(ac_rows[i].second.probe_ns.count());
    fmt::print("{:<24} {:>11.2f}x {:>11.2f}x\n", ac_rows[i].first, rec, eng);
#else
    fmt::print("{:<24} {:>11.2f}x {:>12}\n", ac_rows[i].first, rec, "n/a");
#endif
  }
  return 0;
}
