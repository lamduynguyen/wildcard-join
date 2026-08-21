// Golden result sets for the synthetic ac_bench workloads.
//
// Every other correctness check in this repository compares one of our
// implementations against another one of ours. The fuzzer compares the
// automaton against the recursive matcher, test_reuse compares a reused
// iterator against a fresh one, the benchmark compares all three against each
// other on every run. Those catch a lot, and they all share one blind spot: a
// change that moves both sides together looks like agreement.
//
// This compares against answers computed once, on a known machine, and checked
// into the tree. If a refactor quietly changes which rows match which patterns,
// this is the test that says so, and it says so without needing the reviewer to
// have the old binary.
//
// Two halves, deliberately asymmetric:
//
//   * Checking is cheap. Generate the corpus, run the automaton over it,
//     compare a digest. Seconds, so it belongs in a normal ctest run.
//
//   * Regenerating is expensive and manual. The expected values come from the
//     recursive reference matcher, not from the automaton, so the file is not
//     a recording of whatever the code did last time. That is M patterns times
//     N rows of the slow path and it takes minutes. Set AC_GOLDEN_REGENERATE=1
//     to do it, and read reproducibility/README.md first.
//
// The digests only mean anything because the corpus is a pure function of the
// seed on every standard library.

#include "benchmark/workload.h"
#include "probe.h"

#include "aho_corasick/parser.h"
#include "fmt/format.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef AC_REPRO_DIR
#error "AC_REPRO_DIR is not defined, see test/CMakeLists.txt"
#endif

namespace {

using aho_corasick::TextParserIterator;

const std::string GOLDEN_PATH = std::string(AC_REPRO_DIR) + "/expected/baseline_workloads.tsv";

struct Golden {
  std::string name;
  uint64_t corpus_digest = 0;
  size_t rows            = 0;
  size_t patterns        = 0;
  size_t bytes           = 0;
  size_t pairs           = 0;
  uint64_t result_digest = 0;
};

// The result set digest, defined here rather than anywhere clever because the
// definition is the contract. FNV-1a over the matching pairs, row major, and
// within a row in increasing pattern order, each index folded in as eight
// little endian bytes. Reimplementable in a page of any language, which is the
// requirement for a number an artifact reviewer is asked to trust.
void FoldU64(uint64_t &h, uint64_t v) {
  for (int i = 0; i < 8; i++) {
    h ^= (v >> (i * 8)) & 0xffU;
    h *= workload::FNV_PRIME;
  }
}

// Read the file. Whitespace separated, `#` starts a comment line, one row per
// workload. Not a format, just columns, because the thing most likely to
// happen to this file is a human reading it in a pull request diff.
auto LoadGolden() -> std::vector<Golden> {
  std::ifstream in(GOLDEN_PATH);
  if (!in) { throw std::runtime_error("cannot open " + GOLDEN_PATH); }

  std::vector<Golden> out;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') { continue; }
    std::istringstream ls(line);
    Golden g;
    std::string corpus;
    std::string result;
    ls >> g.name >> corpus >> g.rows >> g.patterns >> g.bytes >> g.pairs >> result;
    if (!ls) { throw std::runtime_error("cannot parse line: " + line); }
    g.corpus_digest = std::stoull(corpus, nullptr, 16);
    g.result_digest = std::stoull(result, nullptr, 16);
    out.push_back(g);
  }
  return out;
}

// Result set for one workload. `use_reference` picks the oracle, which is what
// produced the checked in file, over the automaton, which is what the file
// exists to check.
auto ComputeResults(workload::Workload &w, bool use_reference, size_t &pairs) -> uint64_t {
  probe::BuildSide b(w.patterns);
  std::vector<bool> result(w.patterns.size(), false);
  TextParserIterator iter(b.analyzer.get(), &b.trie);

  uint64_t h = workload::FNV_OFFSET;
  pairs      = 0;
  for (size_t r = 0; r < w.texts.size(); r++) {
    if (use_reference) {
      result = probe::Reference(w.patterns, w.texts[r]);
    } else {
      probe::Drive(b, iter, w.texts[r], result);
    }
    for (size_t p = 0; p < result.size(); p++) {
      if (!result[p]) { continue; }
      pairs++;
      FoldU64(h, r);
      FoldU64(h, p);
    }
  }
  return h;
}

class GoldenWorkload : public testing::TestWithParam<workload::Spec> {};

TEST_P(GoldenWorkload, MatchesCheckedInResultSet) {
  const auto &spec = GetParam();
  auto golden      = LoadGolden();

  auto it = std::find_if(golden.begin(), golden.end(), [&](const Golden &g) { return g.name == spec.name; });
  ASSERT_NE(it, golden.end()) << "no row for " << spec.name << " in " << GOLDEN_PATH;

  auto w = workload::MakeWorkload(spec);

  // Corpus first. If this fails the result digest will fail too and the result
  // digest is the less useful error message: it says the answers are wrong when
  // what is actually wrong is the question.
  EXPECT_EQ(w.digest, it->corpus_digest) << spec.name
                                         << ": the generated corpus is not the one the golden file was "
                                            "built from, so nothing below is a correctness result.";
  EXPECT_EQ(w.texts.size(), it->rows);
  EXPECT_EQ(w.patterns.size(), it->patterns);
  EXPECT_EQ(w.total_text_bytes, it->bytes);

  size_t pairs = 0;
  auto digest  = ComputeResults(w, /*use_reference=*/false, pairs);
  EXPECT_EQ(pairs, it->pairs);
  EXPECT_EQ(digest, it->result_digest);
}

INSTANTIATE_TEST_SUITE_P(Baseline, GoldenWorkload, testing::ValuesIn(workload::BASELINE_SPECS),
                         [](const testing::TestParamInfo<workload::Spec> &info) {
                           // gtest wants an identifier, and the workload names
                           // have `=` in them.
                           std::string s(info.param.name);
                           for (auto &c : s) {
                             if (c == '=' || c == '.' || c == '-') { c = '_'; }
                           }
                           return s;
                         });

// Not a check. Writes the file the checks read, from the reference matcher,
// and only when asked. Minutes, which is why it is behind an environment
// variable rather than behind a command line flag somebody might set by
// accident while running the suite.
TEST(GoldenRegenerate, WriteExpectedFile) {
  const char *env = std::getenv("AC_GOLDEN_REGENERATE");
  if (env == nullptr || std::string(env) != "1") {
    GTEST_SKIP() << "set AC_GOLDEN_REGENERATE=1 to rewrite " << GOLDEN_PATH;
  }

  std::ofstream out(GOLDEN_PATH);
  ASSERT_TRUE(out) << "cannot write " << GOLDEN_PATH;

  // The count is read off the specs rather than typed. It was typed once, a
  // seventh spec was added, and the file went on claiming six until somebody
  // read it.
  out << "# Result sets for the " << workload::BASELINE_SPECS.size()
      << " ac_bench workloads, computed by the\n"
         "# recursive reference matcher in benchmark/reference.h, not by the automaton\n"
         "# under test.\n"
         "#\n"
         "# Regenerate with AC_GOLDEN_REGENERATE=1 on a TestGolden run. Takes minutes.\n"
         "# See reproducibility/README.md for what the columns mean and what a change\n"
         "# to any of them implies.\n"
         "#\n"
         "# name corpus_digest rows patterns bytes pairs result_digest\n";

  for (const auto &spec : workload::BASELINE_SPECS) {
    auto w       = workload::MakeWorkload(spec);
    size_t pairs = 0;
    auto digest  = ComputeResults(w, /*use_reference=*/true, pairs);
    out << spec.name << '\t' << fmt::format("{:#018x}", w.digest) << '\t' << w.texts.size() << '\t' << w.patterns.size()
        << '\t' << w.total_text_bytes << '\t' << pairs << '\t' << fmt::format("{:#018x}", digest) << '\n';
    fmt::print(stderr, "wrote {} pairs={}\n", spec.name, pairs);
  }
}

}  // namespace

auto main(int argc, char **argv) -> int {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
