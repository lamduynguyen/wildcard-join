#include "matcher.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

#include <string>

TEST(TestMatchingUnicode, All) {
  // Unicode text
  std::u8string text = u8"😀🐍🍕 Привет мир こんにちは世界";

  // Patterns to test
  std::vector<std::pair<std::u8string, bool>> tests = {
    {u8"______________________", true},
    // // ---- Exact substring and literal comparisons ----
    // {u8"😀🐍🍕 Привет мир こんにちは世界", true},
    // {u8"😀🐍🍕 Привет мир こんにちは", false},
    // {u8"Привет мир こんにちは", false},

    // // ---- Basic wildcard (% and _) matching ----
    // {u8"😀%", true},
    // {u8"%世界%", true},
    // {u8"😀_🍕_Привет%", true},  // "_" matches single characters
    // {u8"😀_🍕_ 世界%", false},  // mismatch

    // // ---- Multiple underscores ----
    // {u8"😀🐍_", false},
    // {u8"Привет___", false},
    // {u8"%Приве____р%", true},

    // // ---- Multiple percent blocks ----
    // {u8"%Привет%世界%", true},
    // {u8"%こんにちは%Привет%", false},  // ordering wrong
    // {u8"%🍕🐍%Привет%", false},
    // {u8"%🐍🍕%Привет%", true},

    // // ---- Mixed % and _ wildcards ----
    // {u8"%мир%", true},
    // {u8"%😀_🍕%", true},
    // {u8"%🐍_🍕%", false},
    // {u8"%🐍_😀%", false},

    // // ---- Backtracking-heavy patterns ----
    // {u8"%😀%😀%😀%", false},  // repetitive
    // {u8"%🍕%🐍%", false},
    // {u8"%🐍%🍕%", true},  // ordering wrong

    // // ---- Long suffix/prefix tests ----
    // {u8"%Привет мир こんにちは%", true},
    // {u8"😀🐍🍕 Привет%", true},
    // {u8"😀🐍🍕 Привет мир こん___世界", true},
    // {u8"😀🐍🍕 Привет мир こんにちは世界.%🍕%", false},

    // // ---- Empty / nearly empty pattern behavior ----
    // {u8"%", true},
    // {u8"%%", true},
    // {u8"", false},

    // // ---- Deep recursion: % followed by long literal ----
    // {u8"%emojis, Cyrillic, Japanese should match incorrectly.", false},

    // // ---- Multiple % requiring step-by-step backtracking ----
    // {u8"%Привет%мир%", true},
    // {u8"%Привет%мир%😀%", false},
    // {u8"%😀%later%мир%", false},

    // // ---- Boundary behaviors ----
    // {u8"😀🐍🍕 Привет мир こんにちは世界%", true},
    // {u8"😀🐍🍕 Привет мир こんにちは世界_", false},

    // // ---- Complex cases combining everything ----
    // {u8"%😀_🍕%мир%こん_%界", true},
    // {u8"%😀_🍕%мир%こん_%wrong", false},

    // // ---- Adjacent wildcard edge cases ----
    // {u8"%%%%😀%%%%", true},
    // {u8"____", false},    // needs 4 characters
    // {u8"%_%_%_%", true},  // interleaved wildcards
    // {u8"%__", true},      // at least 2 chars anywhere
    // {u8"__%", true},      // at least 2 chars prefix
    // {u8"_%_%_", true},    // alternating single/any

    // // ---- Greedy vs minimal % consumption ----
    // {u8"%🐍🍕 Привет%", true},
    // {u8"%🐍🍕%мир%", true},
    // {u8"%🐍🍕%世界%", true},
    // {u8"%🍕🐍%こんにちは%", false},  // ordering violation
    // {u8"%Привет%🐍🍕%世界%", false},
    // {u8"%Привет%世界%🐍🍕%", false},

    // // ---- Long literal anchors ----
    // {u8"%Привет мир こんにちは世界%", true},
    // {u8"%Привет мир こんにちは世%", true},
    // {u8"%Привет мир こんにちは世界.%", false},
    // {u8"%Привет мир こんにちは世界._", false},

    // // ---- Overlapping literals ----
    // {u8"%こんにちは世界%", true},
    // {u8"%世界世界%", false},  // repeated literal mismatch
    // {u8"%Привет%мир%", true},
    // {u8"%Привет%и мир%", false},
    // {u8"%мир こんにちは%世界%", true},

    // // ---- Underscore precision ----
    // {u8"😀🐍🍕 Привет мир こんにち_世界", true},
    // {u8"😀🐍🍕 プ_вет мир こんにちは世界", false},
    // {u8"😀_🐍🍕 Привет мир こんにちは世界", false},
    // {u8"😀_🐍🍕 Привет мир こんにちは__世界", false},
    // {u8"😀_🐍🍕 Привет мир こんにちは___", false},

    // // ---- Alternating wildcard chains ----
    // {u8"%_%_%_%_%_%_%", true},
    // {u8"%_%_%_%_%_%_x", false},
    // {u8"_%_%_%_%_%_%_", true},
    // {u8"_%_%_%_%_%_%_x", false},

    // // ---- Prefix and suffix strictness ----
    // {u8"😀%", true},
    // {u8"%世界", true},
    // {u8"世界.%", false},
    // {u8"😀🐍🍕%", true},
    // {u8"🐍🍕%", false},

    // // ---- Lockstep matching ----
    // {u8"______________________", true},
    // {u8"________________________", false},

    // // ---- Catastrophic backtracking stress ----
    // {u8"%😀%🐍%🍕%Привет%мир%こんにちは%世界%", true},
    // {u8"%😀%🐍%🍕%Привет%мир%こんにちは%wrong%", false},

    // // ---- Fully anchored but subtle ----
    // {u8"%😀🐍🍕%Привет%мир%こんにちは%世界%", true},
    // {u8"%😀🐍🍕%Привет%мир%こんにちは%wrong%", false},
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
