#include "matcher.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

#include <string>

#define RUN_AHOCORASICK_ESCAPE_TEST(text, patterns, tests, escape_char)                                              \
  {                                                                                                                  \
    std::transform(tests.begin(), tests.end(), std::back_inserter(patterns), [](const auto &p) { return p.first; }); \
    auto results =                                                                                                   \
      AhoCorasickMultiplePatterns(reinterpret_cast<char *>(text.data()), text.size(), patterns, escape_char);        \
    for (auto idx = 0UL; idx < tests.size(); idx++) {                                                                \
      auto &[pat, result] = tests[idx];                                                                              \
      if (results[idx] != result) {                                                                                  \
        fmt::print("AhoCorasick Multi matching: evaluate pattern '{}' return wrong result\n",                        \
                   reinterpret_cast<const char *>(pat.data()));                                                      \
      }                                                                                                              \
      EXPECT_EQ(results[idx], result);                                                                               \
    }                                                                                                                \
  }

TEST(TestMatching, ASCII) {
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
    {"the%quick%fox%", true},        // “the” => later “quick” => later “fox”
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
  RUN_AHOCORASICK_ESCAPE_TEST(text, patterns, tests, ""sv);
}

TEST(TestMatching, BasicUnicode) {
  // Unicode text
  std::u8string text = u8"😀🐍🍕 Привет мир こんにちは世界";

  // Patterns to test
  std::vector<std::pair<std::u8string, bool>> tests = {
    // ---- Exact substring and literal comparisons ----
    {u8"😀🐍🍕 Привет мир こんにちは世界", true},
    {u8"😀🐍🍕 Привет мир こんにちは", false},
    {u8"Привет мир こんにちは", false},

    // ---- Basic wildcard (% and _) matching ----
    {u8"😀%", true},
    {u8"%世界%", true},
    {u8"😀_🍕_Привет%", true},  // "_" matches single characters
    {u8"😀_🍕_ 世界%", false},  // mismatch

    // ---- Multiple underscores ----
    {u8"😀🐍_", false},
    {u8"Привет___", false},
    {u8"%Приве____р%", true},

    // ---- Multiple percent blocks ----
    {u8"%Привет%世界%", true},
    {u8"%こんにちは%Привет%", false},  // ordering wrong
    {u8"%🍕🐍%Привет%", false},
    {u8"%🐍🍕%Привет%", true},

    // ---- Mixed % and _ wildcards ----
    {u8"%мир%", true},
    {u8"%😀_🍕%", true},
    {u8"%🐍_🍕%", false},
    {u8"%🐍_😀%", false},

    // ---- Backtracking-heavy patterns ----
    {u8"%😀%😀%😀%", false},  // repetitive
    {u8"%🍕%🐍%", false},
    {u8"%🐍%🍕%", true},  // ordering wrong

    // ---- Long suffix/prefix tests ----
    {u8"%Привет мир こんにちは%", true},
    {u8"😀🐍🍕 Привет%", true},
    {u8"😀🐍🍕 Привет мир こん___世界", true},
    {u8"😀🐍🍕 Привет мир こんにちは世界.%🍕%", false},

    // ---- Empty / nearly empty pattern behavior ----
    {u8"%", true},
    {u8"%%", true},
    {u8"", false},

    // ---- Deep recursion: % followed by long literal ----
    {u8"%emojis, Cyrillic, Japanese should match incorrectly.", false},

    // ---- Multiple % requiring step-by-step backtracking ----
    {u8"%Привет%мир%", true},
    {u8"%Привет%мир%😀%", false},
    {u8"%😀%later%мир%", false},

    // ---- Boundary behaviors ----
    {u8"😀🐍🍕 Привет мир こんにちは世界%", true},
    {u8"😀🐍🍕 Привет мир こんにちは世界_", false},

    // ---- Complex cases combining everything ----
    {u8"%😀_🍕%мир%こん_%界", true},
    {u8"%😀_🍕%мир%こん_%wrong", false},

    // ---- Adjacent wildcard edge cases ----
    {u8"%%%%😀%%%%", true},
    {u8"____", false},    // needs 4 characters
    {u8"%_%_%_%", true},  // interleaved wildcards
    {u8"%__", true},      // at least 2 chars anywhere
    {u8"__%", true},      // at least 2 chars prefix
    {u8"_%_%_", true},    // alternating single/any

    // ---- Greedy vs minimal % consumption ----
    {u8"%🐍🍕 Привет%", true},
    {u8"%🐍🍕%мир%", true},
    {u8"%🐍🍕%世界%", true},
    {u8"%🍕🐍%こんにちは%", false},  // ordering violation
    {u8"%Привет%🐍🍕%世界%", false},
    {u8"%Привет%世界%🐍🍕%", false},

    // ---- Long literal anchors ----
    {u8"%Привет мир こんにちは世界%", true},
    {u8"%Привет мир こんにちは世%", true},
    {u8"%Привет мир こんにちは世界.%", false},
    {u8"%Привет мир こんにちは世界._", false},

    // ---- Overlapping literals ----
    {u8"%こんにちは世界%", true},
    {u8"%世界世界%", false},  // repeated literal mismatch
    {u8"%Привет%мир%", true},
    {u8"%Привет%и мир%", false},
    {u8"%мир こんにちは%世界%", true},

    // ---- Underscore precision ----
    {u8"😀🐍🍕 Привет мир こんにち_世界", true},
    {u8"😀🐍🍕 プ_вет мир こんにちは世界", false},
    {u8"😀_🐍🍕 Привет мир こんにちは世界", false},
    {u8"😀_🐍🍕 Привет мир こんにちは__世界", false},
    {u8"😀_🐍🍕 Привет мир こんにちは___", false},

    // ---- Alternating wildcard chains ----
    {u8"%_%_%_%_%_%_%", true},
    {u8"%_%_%_%_%_%_x", false},
    {u8"_%_%_%_%_%_%_", true},
    {u8"_%_%_%_%_%_%_x", false},

    // ---- Prefix and suffix strictness ----
    {u8"😀%", true},
    {u8"%世界", true},
    {u8"世界.%", false},
    {u8"😀🐍🍕%", true},
    {u8"🐍🍕%", false},

    // ---- Lockstep matching ----
    {u8"______________________", true},
    {u8"________________________", false},

    // ---- Catastrophic backtracking stress ----
    {u8"%😀%🐍%🍕%Привет%мир%こんにちは%世界%", true},
    {u8"%😀%🐍%🍕%Привет%мир%こんにちは%wrong%", false},

    // ---- Fully anchored but subtle ----
    {u8"%😀🐍🍕%Привет%мир%こんにちは%世界%", true},
    {u8"%😀🐍🍕%Привет%мир%こんにちは%wrong%", false},
  };

  // DuckDB Matching
  for (auto &[pat, result] : tests) {
    auto try_pat = DuckDBMatching(reinterpret_cast<char *>(text.data()), text.size(),
                                  reinterpret_cast<char *>(pat.data()), pat.size());
    if (try_pat != result) {
      fmt::print("DuckDB: evaluate pattern '{}' return wrong result\n", reinterpret_cast<const char *>(pat.data()));
    }
    EXPECT_EQ(try_pat, result);
  }

  // Greedy Matching
  for (auto &[pat, result] : tests) {
    auto try_pat = GreedyMatching(reinterpret_cast<char *>(text.data()), text.size(),
                                  reinterpret_cast<char *>(pat.data()), pat.size());
    if (try_pat != result) {
      fmt::print("Greedy: evaluate pattern '{}' return wrong result\n", reinterpret_cast<const char *>(pat.data()));
    }
    EXPECT_EQ(try_pat, result);
  }

  // Multi-pattern matching
  std::vector<std::u8string> patterns;
  RUN_AHOCORASICK_ESCAPE_TEST(text, patterns, tests, ""sv);
}

TEST(TestMatching, EscapeCharacter) {
  // text in-memory: '%', 'a', '_', 'b'  (4 chars)
  std::u8string text                                = u8"%a_b";
  std::vector<std::pair<std::u8string, bool>> tests = {
    // ---- Basic literal escaping ----
    {u8"!%a!_b", true},  // literal '%', 'a', literal '_', 'b'
    {u8"!%a!_%", true},  // literal '%', 'a', literal '_', then wildcard
    {u8"!%_!_b", true},  // literal '%', wildcard '_', literal '_', 'b'
    {u8"!%a_b", true},   // literal '%', 'a', wildcard '_', 'b'
    {u8"!%a!_b", true},  // literal '%', 'a', literal '_', 'b'
    {u8"!%a!_%", true},  // literal '%', 'a', literal '_', wildcard
    {u8"!%_!_b", true},  // literal '%', wildcard, literal '_', 'b'
    {u8"____", true},    // four wildcards
    {u8"!%a_b", true},   // literal '%', 'a', wildcard for '_', 'b'
    {u8"!%ab", false},   // literal '%', 'ab' -- missing '_'
    {u8"!_ab", false},   // literal '_' at start -- text starts with '%'
    {u8"_____", false},  // five wildcards -- text is only 4 chars

    // ---- Wildcard-only ----
    {u8"____", true},    // exactly 4 wildcards match 4 chars
    {u8"_____", false},  // 5 wildcards -- text is only 4 chars
    {u8"___", false},    // 3 wildcards -- too short
    {u8"%", true},       // single % matches anything
    {u8"%%", true},      // redundant % still matches
    {u8"%_%_%", true},   // at least 2 chars anywhere

    // ---- Escaped wildcards that must match literally ----
    {u8"!%ab", false},    // literal '%', 'ab' -- missing '_'
    {u8"!_ab", false},    // literal '_' at start -- text starts with '%'
    {u8"!%!_ab", false},  // literal '%', literal '_', 'ab' -- text has 'a' not '_' after '%'
    {u8"a!_b", false},    // 'a', literal '_', 'b' -- text starts with '%' not 'a'

    // ---- Self-escape (!! => literal !) ----
    {u8"!!%a!_b", false},  // literal '!', literal '%', 'a', literal '_', 'b' -- text has no leading '!'
    {u8"%!!%", false},     // wildcard, literal '!', wildcard -- text has no '!' anywhere
    {u8"!%a!!b", false},   // literal '%', 'a', literal '!', 'b' -- text has '_' not '!'

    // ---- Percent wildcards with escaped anchors ----
    {u8"!%a%", true},    // literal '%', 'a', then anything
    {u8"!%a%b", true},   // literal '%', 'a', anything, 'b'
    {u8"!%a%x", false},  // literal '%', 'a', anything, 'x' -- text ends in 'b'
    {u8"%!_b", true},    // anything, literal '_', 'b'
    {u8"%!_%", true},    // anything, literal '_', anything
    {u8"%!_x", false},   // anything, literal '_', 'x' -- text has 'b' not 'x'
    {u8"%!%", false},
    {u8"%!%b", false},
    {u8"%!%%", true},  // anything, literal '%', anything

    // ---- Escape at pattern boundaries ----
    {u8"!%a!_b%", true},  // full match with trailing wildcard
    {u8"%!%a!_b", true},  // leading wildcard then literal '%', 'a', literal '_', 'b'
    {u8"!%a!_", false},   // literal '%', 'a', literal '_' -- missing trailing 'b'

    // ---- Underscore counting with escapes ----
    {u8"_!_b", false},  // one wildcard then literal '_', 'b' -- wildcard consumes '%',
                        // then literal '_' needs '_' but text has 'a' at pos 1
    {u8"_a!_b", true},  // wildcard matches '%', then 'a', literal '_', 'b'
    {u8"_a_b", true},   // wildcard '%', 'a', wildcard '_', 'b'
    {u8"__!_b", true},

    // ---- Escape character not before a wildcard (treated as literal) ----
    {u8"!a_b", false},  // '!' is not an escape here ('a' is not escapable) => literal '!','a'... text has no '!'
    {u8"%!ab", false},  // wildcard, then '!','a','b' -- '!' not before wildcard, so literal '!'
                        // text has no '!' character
  };
  std::vector<std::u8string> patterns;
  RUN_AHOCORASICK_ESCAPE_TEST(text, patterns, tests, "!"sv);
}

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
