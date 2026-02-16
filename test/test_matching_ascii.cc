#include "matcher.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

#include <cstdlib>
#include <deque>
#include <queue>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

TEST(TestMatchingASCII, All) {
  std::string text = "the quick brown fox jumps over the lazy dog.";

  std::vector<std::pair<std::string, bool>> tests = {
    // ---- Exact substring and literal comparisons ----
    {"the quick brown fox jumps over the lazy dog.", true},
    {"the quick brown fox jumps over the lazy dog", false},
    {"the quick brown fox appears again", false},

    // ---- Basic wildcard (% and _) matching ----
    {"the%", true},
    {"%lazy dog%", true},
    {"the quick brown fox _umps%", true},  // "_" matches 'j'
    {"the quick brown fox _x%", false},    // "_x" mismatches 'fo'

    // ---- Multiple underscores ----
    {"the quick brown f_x", false},
    {"the quick brown f___", false},
    {"the quick brown fo_%", true},  // matches "fox"

    // ---- Multiple percent blocks ----
    {"the%fox%%", true},  // scattered matches across text
    {"%brown%over%dog%", true},
    {"%brown%never%", false},  // “never” not present

    // ---- Mixed % and _ wildcards ----
    {"the%lazy%", true},
    {"%quick%b__wn%", true},         // matches "brown"
    {"%quick%b__xn%", false},        // wrong letter
    {"the%quick%fox%", true},        // “the” → later “quick” → later “fox”
    {"the%quick%elephant%", false},  // “elephant” absent

    // ---- Backtracking-heavy patterns ----
    {"%the%the%the%", false},  // repetitive
    {"%fox%jump%", true},
    {"%jump%fox%", false},  // ordering wrong

    // ---- Long suffix/prefix tests ----
    {"%typing practice.%", false},
    {"the quick brown fox jumps%", true},
    {"the quick brown fox jumps over the______dog.", true},
    {"the quick brown fox jumps over the lazy dog.%aardvark%", false},

    // ---- Empty / nearly empty pattern behavior ----
    {"%", true},
    {"%%", true},
    {"", false},

    // ---- Deep recursion: % followed by long literal ----
    {"%wildcards like percent symbols and underscores should match incorrectly.", false},

    // ---- Multiple % requiring step-by-step backtracking ----
    {"%quick%brown%the%", true},
    {"%the%later%quick%", false},

    // ---- Boundary behaviors ----
    {"the quick brown fox jumps over the lazy dog.%", true},
    {"the quick brown fox jumps over the lazy dog._", false},  // trailing _ requires extra character

    // ---- Complex cases combining everything ----
    {"the%q_i_k%b%o%n%f%x%l_z%dog.", true},
    {"%the%q_i_k%b%o%n%f%x%l_z%wrong.", false},

    // ---- Adjacent wildcard edge cases ----
    {"%%%%the%%%%", true},
    {"____", false},    // needs 4 chars
    {"%_%_%_%", true},  // interleaved wildcards
    {"%__", true},      // at least 2 chars anywhere
    {"__%", true},      // at least 2 chars prefix
    {"_%_%_", true},    // alternating single/any

    // ---- Greedy vs minimal % consumption ----
    {"%fox jumps over%", true},
    {"%fox%over%", true},
    {"%fox%lazy%", true},
    {"%fox%quick%", false},  // ordering violation
    {"%quick%fox%lazy%", true},
    {"%quick%lazy%fox%", false},

    // ---- Long literal anchors ----
    {"%quick brown fox jumps over the lazy%", true},
    {"%quick brown fox jumps over the lazi%", false},
    {"%quick brown fox jumps over the lazy dog.%", true},
    {"%quick brown fox jumps over the lazy dog._", false},

    // ---- Overlapping literals ----
    {"%the lazy dog.%", true},
    {"%lazy dog.lazy%", false},  // repeated literal mismatch
    {"%the lazy%lazy dog%", false},
    {"%the lazy%y dog%", false},
    {"%the lazy% dog%", true},
    {"%lazy dog%lazy%", false},

    // ---- Underscore precision ----
    {"the quick brown fox jumps over the lazy d_g.", true},
    {"the quick brown fox jumps over the lazy do_.", true},
    {"the quick brown fox jumps over the lazy _og.", true},
    {"the quick brown fox jumps over the lazy __g.", true},
    {"the quick brown fox jumps over the lazy ___", false},

    // ---- Alternating wildcard chains ----
    {"%_%_%_%_%_%_%", true},
    {"%_%_%_%_%_%_x", false},
    {"_%_%_%_%_%_%_", true},
    {"_%_%_%_%_%_%_x", false},

    // ---- Prefix and suffix strictness ----
    {"the%", true},
    {"%dog.", true},
    {"dog.%", false},
    {"the quick brown%", true},
    {"quick brown%", false},

    // ---- Lockstep matching ----
    {"___________________________", false},                  // too short
    {"____________________________________________", true},  // exact length
    {"___________________________________________.", true},
    {"__________________________________________________________________________________", false},

    // ---- Catastrophic backtracking stress ----
    {"%t%h%e%q%u%i%c%k%b%r%o%w%n%", true},
    {"%t%h%e%q%u%i%c%k%x%", true},
    {"%f%o%x%j%u%m%p%s%", true},
    {"%f%o%x%j%u%m%p%x%", false},

    // ---- Fully anchored but subtle ----
    {"the%q__ck%b%own%f%x%j%mps%over%l_zy%dog.", true},
    {"the%q__ck%b%own%f%x%j%mps%over%l_zy%do_.", true},
  };

  // DuckDB Matching
  for (auto &[pat, result] : tests) {
    auto try_pat = DuckDBMatching(text.c_str(), text.size(), pat.c_str(), pat.size());
    if (try_pat != result) {
      std::cout << "DuckDB: evaluate pattern: '" << pat << "' return wrong result" << std::endl;
    }
    EXPECT_EQ(try_pat, result);
  }

  // Greedy Matching
  for (auto &[pat, result] : tests) {
    auto try_pat = GreedyMatching(text.c_str(), text.size(), pat.c_str(), pat.size());
    if (try_pat != result) {
      std::cout << "Greedy: evaluate pattern: '" << pat << "' return wrong result" << std::endl;
    }
    EXPECT_EQ(try_pat, result);
  }

  // Transform the tests into two vector for multi-pattern matching
  std::vector<std::string> patterns;
  std::transform(tests.begin(), tests.end(), std::back_inserter(patterns), [](const auto &p) { return p.first; });
  auto results = AhoCorasickMultiplePatterns(text.c_str(), text.size(), patterns);
  for (auto idx = 0UL; idx < tests.size(); idx++) {
    auto &[pat, result] = tests[idx];
    if (results[idx] != result) {
      std::cout << "AhoCorasick Multi matching: evaluate pattern: '" << pat << "' return wrong result" << std::endl;
    }
    EXPECT_EQ(results[idx], result);
  }
}

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
