#include <cstdlib>

#include "gtest/gtest.h"

static constexpr auto PERCENTAGE = '%';
static constexpr auto UNDERSCORE = '_';

bool DuckDBMatching(const char *sdata, size_t slen, const char *pdata, size_t plen) {
  size_t pidx = 0;
  size_t sidx = 0;
  for (; pidx < plen && sidx < slen; pidx++) {
    const char &pchar = pdata[pidx];
    const char &schar = sdata[sidx];
    if (pchar == UNDERSCORE) {
      sidx++;
    } else if (pchar == PERCENTAGE) {
      pidx++;
      while (pidx < plen && pdata[pidx] == PERCENTAGE) { pidx++; }
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
  while (pidx < plen && pdata[pidx] == PERCENTAGE) { pidx++; }
  return pidx == plen && sidx == slen;
}

bool DPMatching(const char *sdata, size_t slen, const char *pdata, size_t plen) {
  // dp[i][j] = whether s[i:] matches p[j:]
  bool dp[slen + 1][plen + 1] = {};
  dp[slen][plen]              = true;  // Base case: empty text & empty pattern match

  // handle trailing % at end of pattern
  for (int j = (int)plen - 1; j >= 0; j--) {
    if (pdata[j] == PERCENTAGE) {
      dp[slen][j] = dp[slen][j + 1];  // % can match empty
    } else {
      dp[slen][j] = false;
    }
  }

  // fill DP table bottom-up
  for (int i = (int)slen - 1; i >= 0; i--) {
    for (int j = (int)plen - 1; j >= 0; j--) {
      const char &pchar = pdata[j];

      if (pchar == PERCENTAGE) {
        // two options:
        //    1) % matches zero characters → dp[i][j+1]
        //    2) % matches one character → dp[i+1][j]
        dp[i][j] = dp[i][j + 1] || dp[i + 1][j];
      } else if (pchar == UNDERSCORE) {
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
  int i = 0, j = 0, s_pos = -1, p_pos = -1;
  while (i < slen) {
    if (j < plen && (s[i] == p[j] || p[j] == UNDERSCORE))
      i++, j++;
    else if (j < plen && p[j] == PERCENTAGE)
      s_pos = i, p_pos = j++;
    else if (p_pos != -1)
      i = ++s_pos, j = p_pos + 1;
    else
      return false;
  }
  while (j < plen && p[j] == PERCENTAGE) j++;
  return j == plen;
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

    // ---- Pattern longer than text ----
    {"__________________________________________________________________________________", false},

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
    {"%the%q_i_k%b%o%n%f%x%l_z%wrong.", false}};

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
}

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
