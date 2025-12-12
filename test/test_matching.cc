#include "aho_corasick/aho_corasick.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

#include <cstdlib>
#include <ranges>

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
  auto s_idx          = 0U;
  auto p_idx          = 0U;
  auto star_pos_in_p  = -1U;
  auto last_star_in_s = -1U;
  while (s_idx < slen) {
    if (p_idx < plen && (s[s_idx] == p[p_idx] || p[p_idx] == UNDERSCORE))
      p_idx++, s_idx++;
    else if (p_idx < plen && p[p_idx] == PERCENTAGE)
      last_star_in_s = s_idx, star_pos_in_p = p_idx++;
    else if (star_pos_in_p != -1)
      s_idx = ++last_star_in_s, p_idx = star_pos_in_p + 1;
    else
      return false;
  }
  while (p_idx < plen && p[p_idx] == PERCENTAGE) p_idx++;
  return p_idx == plen;
}

struct Token {
  std::string view;
  std::size_t start;  // offset in the original string
};

bool AhoCorasickMatching(const char *s, size_t slen, const char *p, size_t plen) {
  // Aho-Corasick env
  auto trie = aho_corasick::AhoCorasick();
  auto t    = trie.Local();

  // Split patterns into keywords, separated by % and _, and then insert into AhoCorasick's trie
  std::unordered_map<u64, u64> substr_size;
  auto strw       = std::string_view(p, plen);
  auto is_delim   = [](char c) { return c == UNDERSCORE || c == PERCENTAGE; };
  auto next_token = [&](std::string_view s, std::size_t &pos) -> Token {
    while (pos < s.size() && is_delim(s[pos])) pos++;
    if (pos >= s.size()) return {{}, std::string_view::npos};  // no more tokens
    std::size_t start = pos;
    while (pos < s.size() && !is_delim(s[pos])) pos++;
    return {std::string(s.substr(start, pos - start)) + '\0', start};
  };
  size_t pos = 0;

  while (true) {
    auto tok = next_token(strw, pos);
    if (tok.start == std::string::npos) { break; }
    trie.Insert(tok.view.data(), tok.view.size(), {0, tok.start}, t);
    substr_size[tok.start] = tok.view.size() - 1;
  }

  // Building suffix & output links
  trie.BuildSuffixLink(1);

  // Get substring matcher info from
  auto ac_matchers    = trie.ParseText(std::string_view(s, slen));
  auto s_idx          = 0U;
  auto p_idx          = 0U;
  auto star_pos_in_p  = -1U;
  auto last_star_in_s = -1U;

  while (s_idx < slen) {
    if (p_idx < plen && p[p_idx] == UNDERSCORE) {
      p_idx++;
      s_idx++;
    } else if (p_idx < plen && p[p_idx] == PERCENTAGE) {
      last_star_in_s = s_idx;
      star_pos_in_p  = p_idx++;
    } else {
      auto pattern_len = substr_size[p_idx];
      auto tmp         = aho_corasick::PatternIndexType(0, p_idx);
      auto output_tp   = aho_corasick::MatchingOutputType(tmp, s_idx + pattern_len - 1);
      auto matcher_ptr = ac_matchers.find(output_tp);
      if (p_idx < plen && matcher_ptr != ac_matchers.end()) {
        assert(std::memcmp(&s[s_idx], &p[p_idx], pattern_len) == 0);
        p_idx += pattern_len;
        s_idx += pattern_len;
      } else if (star_pos_in_p != -1) {
        s_idx = ++last_star_in_s;
        p_idx = star_pos_in_p + 1;
      } else {
        return false;
      }
    }
  }
  while (p_idx < plen && p[p_idx] == PERCENTAGE) { p_idx++; }
  return p_idx == plen;
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
  for (auto &[pat, result] : tests) {
    auto try_pat = AhoCorasickMatching(text.c_str(), text.size(), pat.c_str(), pat.size());
    if (try_pat != result) {
      std::cout << "AhoCorasick: evaluate pattern: '" << pat << "' return wrong result" << std::endl;
    }
    EXPECT_EQ(try_pat, result);
  }
}

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
