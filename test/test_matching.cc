#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/skeleton.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

#include <cstdlib>
#include <deque>
#include <queue>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

bool DuckDBMatching(const char *sdata, size_t slen, const char *pdata, size_t plen) {
  size_t pidx = 0;
  size_t sidx = 0;
  for (; pidx < plen && sidx < slen; pidx++) {
    const char &pchar = pdata[pidx];
    const char &schar = sdata[sidx];
    if (pchar == aho_corasick::UNDERSCORE) {
      sidx++;
    } else if (pchar == aho_corasick::PERCENTAGE) {
      pidx++;
      while (pidx < plen && pdata[pidx] == aho_corasick::PERCENTAGE) { pidx++; }
      if (pidx == plen) { return true; /* tail is acceptable */ }
      for (; sidx < slen; sidx++) {
        if (DuckDBMatching(sdata + sidx, slen - sidx, pdata + pidx, plen - pidx)) { return true; }
      }
      return false;
    } else if (pchar == schar) {
      sidx++;
    } else {
      return false;
    }
  }
  while (pidx < plen && pdata[pidx] == aho_corasick::PERCENTAGE) { pidx++; }
  return pidx == plen && sidx == slen;
}

bool DPMatching(const char *sdata, size_t slen, const char *pdata, size_t plen) {
  // dp[i][j] = whether s[i:] matches p[j:]
  bool dp[slen + 1][plen + 1] = {};
  dp[slen][plen]              = true;  // Base case: empty text & empty pattern match

  // handle trailing % at end of pattern
  for (int j = (int)plen - 1; j >= 0; j--) {
    if (pdata[j] == aho_corasick::PERCENTAGE) {
      dp[slen][j] = dp[slen][j + 1];  // % can match empty
    } else {
      dp[slen][j] = false;
    }
  }

  // fill DP table bottom-up
  for (int i = (int)slen - 1; i >= 0; i--) {
    for (int j = (int)plen - 1; j >= 0; j--) {
      const char &pchar = pdata[j];

      if (pchar == aho_corasick::PERCENTAGE) {
        // two options:
        //    1) % matches zero characters → dp[i][j+1]
        //    2) % matches one character → dp[i+1][j]
        dp[i][j] = dp[i][j + 1] || dp[i + 1][j];
      } else if (pchar == aho_corasick::UNDERSCORE) {
        // "_" must match exactly one character
        dp[i][j] = dp[i + 1][j + 1];
      } else {
        // literal character
        if (pchar == sdata[i]) {
          dp[i][j] = dp[i + 1][j + 1];
        } else {
          dp[i][j] = false;
        }
      }
    }
  }

  // Final result: does s[0:] match p[0:] ?
  return dp[0][0];
}

bool GreedyMatching(const char *s, size_t slen, const char *p, size_t plen) {
  auto s_idx          = 0U;
  auto p_idx          = 0U;
  auto star_pos_in_p  = -1U;
  auto last_star_in_s = -1U;
  while (s_idx < slen) {
    if (p_idx < plen && (s[s_idx] == p[p_idx] || p[p_idx] == aho_corasick::UNDERSCORE))
      p_idx++, s_idx++;
    else if (p_idx < plen && p[p_idx] == aho_corasick::PERCENTAGE)
      last_star_in_s = s_idx, star_pos_in_p = p_idx++;
    else if (star_pos_in_p != -1)
      s_idx = ++last_star_in_s, p_idx = star_pos_in_p + 1;
    else
      return false;
  }
  while (p_idx < plen && p[p_idx] == aho_corasick::PERCENTAGE) p_idx++;
  return p_idx == plen;
}

bool AhoCorasickMatching(const char *s, size_t slen, const char *p, size_t plen) {
  // Aho-Corasick env
  auto trie = aho_corasick::AhoCorasick();
  auto t    = trie.Local();

  // Pre-processing the pattern into pattern skeleton, then insert the split literals into AhoCorasick's trie
  auto skeleton = aho_corasick::Skeleton(
    p, plen, [&](aho_corasick::Token &tok) { trie.Insert(p + tok.start, tok.len, {0, tok.start, tok.len}, t); });
  if (skeleton.IsEmpty() || skeleton.OnlyWildcard()) {
    return aho_corasick::Skeleton::SpecialMatchEmptyPattern(slen, p, plen);
  }

  // Building suffix & output links
  trie.BuildSuffixLink(1);

  // Start matching text
  aho_corasick::Skeleton::Matcher instance = {};
  auto iterate                             = trie.StartIterativeParseText(s, slen);
  for (auto end_offset = 0UL; end_offset < slen; end_offset++) {
    auto ac_matchers = trie.ContinueParseText(iterate);
    auto &sket       = skeleton[instance.segment_idx];
    for (auto &match : ac_matchers) {
      // Must match within the current considerate pattern
      // Focus on the comparison: match.text_start_pos >= instance.min_text_start_pos
      if (sket.Contain(match.pattern_index.start_pos) && match.text_start_pos >= instance.min_text_start_pos) {
        auto success = skeleton.TryMatching(match, instance);

        // Now, check if we just insert the last match of the sket
        if (success && sket.IsLastLiteral(match.pattern_index.start_pos) &&
            skeleton.SatisfyMatcher(match, instance, slen)) {
          instance.AdvanceNextSegment(end_offset + sket.suffix_underscore_cnt + 1);
          if (instance.segment_idx >= skeleton.Size()) { return true; }
        }
      }
    }
  }
  return false;
}

auto AhoCorasickMultiplePatterns(const char *s, size_t slen, std::vector<std::string> patterns) -> std::vector<bool> {
  // Aho-Corasick env
  auto trie = aho_corasick::AhoCorasick();
  auto t    = trie.Local();
  std::vector<bool> result(patterns.size(), false);

  // Pre-processing the pattern into pattern skeleton, then insert the split literals into AhoCorasick's trie
  std::vector<aho_corasick::Skeleton> skeleton;
  for (auto idx = 0UL; idx < patterns.size(); idx++) {
    auto &pat = patterns[idx];
    skeleton.emplace_back(pat.c_str(), pat.size(), [&](aho_corasick::Token &tok) {
      trie.Insert(pat.c_str() + tok.start, tok.len, {idx, tok.start, tok.len}, t);
    });
    if (skeleton.back().IsEmpty() || skeleton.back().OnlyWildcard()) {
      result[idx] = aho_corasick::Skeleton::SpecialMatchEmptyPattern(slen, pat.c_str(), pat.size());
    }
  }

  // Building suffix & output links
  trie.BuildSuffixLink(1);

  // Start matching text
  std::vector<aho_corasick::Skeleton::Matcher> instance(patterns.size());
  auto iterate = trie.StartIterativeParseText(s, slen);
  for (auto end_offset = 0UL; end_offset < slen; end_offset++) {
    auto ac_matchers = trie.ContinueParseText(iterate);

    for (auto &match : ac_matchers) {
      auto pat_id   = match.pattern_index.pattern_id;
      auto &sket    = skeleton[pat_id];
      auto &matcher = instance[pat_id];

      if (!result[pat_id] && sket.MayMatch(match, matcher)) {
        auto success = sket.TryMatching(match, matcher);

        if (success) {
          // TODO: possibly maintain a per-pattern ordered map that contains
          //   the expected text offset to the next literal per pattern id
          // Based on this mapping, we can eagerly garbage collect the matching info based on `end_offset`

          // Now, check if we just insert the last match of the sket
          if (sket[matcher.segment_idx].IsLastLiteral(match.pattern_index.start_pos) &&
              sket.SatisfyMatcher(match, matcher, slen)) {
            matcher.AdvanceNextSegment(end_offset + sket[matcher.segment_idx].suffix_underscore_cnt + 1);
            if (matcher.segment_idx >= sket.Size()) { result[pat_id] = true; }
          }
        }
      }
    }
  }

  return result;
}

TEST(TestMatching, All) {
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

  // DP Matching
  for (auto &[pat, result] : tests) {
    auto try_pat = DPMatching(text.c_str(), text.size(), pat.c_str(), pat.size());
    if (try_pat != result) { std::cout << "DP: evaluate pattern: '" << pat << "' return wrong result" << std::endl; }
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

  // AhoCorasick matching
  for (auto &[pat, result] : tests) {
    auto try_pat = AhoCorasickMatching(text.c_str(), text.size(), pat.c_str(), pat.size());
    if (try_pat != result) {
      std::cout << "AhoCorasick: evaluate pattern: '" << pat << "' return wrong result" << std::endl;
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
