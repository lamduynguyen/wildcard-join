#include "matcher.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

#include <string>

TEST(TestMatchingUnicode, All) {
  // Unicode text
  std::u8string text = u8"😀🐍🍕 Привет мир こんにちは世界";

  // Patterns to test
  std::vector<std::pair<std::u8string, bool>> tests = {
    // ---- Exact match ----
    {u8"😀🐍🍕 Привет мир こんにちは世界", true},
    {u8"😀🐍🍕 Привет мир こんにちは", false},
    {u8"Привет мир", false},

    // ---- Basic wildcard matching ----
    {u8"😀%", true},
    {u8"%мир%", true},
    {u8"😀_🍕%", true},        // "_" matches '🐍'
    {u8"Привет_мир%", false},  // "_" mismatch

    // ---- Multiple underscores ----
    {u8"😀🐍_", false},        // trailing character missing
    {u8"Привет___", false},    // too short
    {u8"%Привет_мир%", true},  // matches "Привет мир"

    // ---- Multiple percent blocks ----
    {u8"%Привет%世界", true},          // scattered matches
    {u8"%こんにちは%Привет%", false},  // ordering wrong
    {u8"%🍕🐍%Привет%", false},
    {u8"%🐍🍕%Привет%", true},

    // ---- Mixed % and _ wildcards ----
    {u8"%мир%", true},
    {u8"%😀_🍕%", true},
    {u8"%🐍_🍕%", false},
    {u8"%🐍_😀%", false},

    // ---- Empty / nearly empty pattern behavior ----
    {u8"%", true},
    {u8"%%", true},
    {u8"", false},

    // ---- Complex unicode pattern ----
    {u8"%😀_🍕%мир%世界", true},
    {u8"%😀_🍕%мир%世_", true},
  };

  // DuckDB Matching
  for (auto &[pat, result] : tests) {
    auto try_pat = DuckDBMatching(reinterpret_cast<char *>(text.data()), text.size(),
                                  reinterpret_cast<char *>(pat.data()), pat.size());
    if (try_pat != result) {
      fmt::println("DuckDB: evaluate pattern '{}' return wrong result", reinterpret_cast<const char *>(pat.data()));
    }
    EXPECT_EQ(try_pat, result);
  }

  // Greedy Matching
  for (auto &[pat, result] : tests) {
    auto try_pat = GreedyMatching(reinterpret_cast<char *>(text.data()), text.size(),
                                  reinterpret_cast<char *>(pat.data()), pat.size());
    if (try_pat != result) {
      fmt::println("Greedy: evaluate pattern '{}' return wrong result", reinterpret_cast<const char *>(pat.data()));
    }
    EXPECT_EQ(try_pat, result);
  }

  // AhoCorasick matching
  for (auto &[pat, result] : tests) {
    auto try_pat = AhoCorasickMatching(reinterpret_cast<char *>(text.data()), text.size(),
                                       reinterpret_cast<char *>(pat.data()), pat.size());
    if (try_pat != result) {
      fmt::println("AhoCorasick: evaluate pattern '{}' return wrong result",
                   reinterpret_cast<const char *>(pat.data()));
    }
    EXPECT_EQ(try_pat, result);
  }

  // // Multi-pattern matching
  // std::vector<std::u8string> patterns;
  // std::transform(tests.begin(), tests.end(), std::back_inserter(patterns), [](const auto &p) { return p.first; });
  // auto results = AhoCorasickMultiplePatterns(reinterpret_cast<char *>(text.data()), text.size(), patterns);
  // for (auto idx = 0UL; idx < tests.size(); idx++) {
  //   auto &[pat, result] = tests[idx];
  //   if (results[idx] != result) {
  //     fmt::println("AhoCorasick Multi matching: evaluate pattern '{}' return wrong result",
  //                  reinterpret_cast<const char *>(pat.data()));
  //   }
  //   EXPECT_EQ(results[idx], result);
  // }
}

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
