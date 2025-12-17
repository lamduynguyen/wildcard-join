#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/skeleton.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

#include <cstdlib>
#include <deque>
#include <queue>
#include <ranges>
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

struct ActiveMatch {
  struct PairHash {
    std::size_t operator()(const std::pair<u64, u64> &p) const noexcept {
      // simple but effective hash combine
      std::size_t h1 = std::hash<u64>{}(p.first);
      std::size_t h2 = std::hash<u64>{}(p.second);
      return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
  };

  u64 underscore_left = 0;
  u64 skeleton_idx    = 0;
  std::unordered_set<std::pair<u64, u64>, PairHash> match;  // A pair of s_idx, p_idx
};

bool AhoCorasickMatchingOptimize(const char *s, size_t slen, const char *p, size_t plen) {
  // Aho-Corasick env
  fmt::println("==========Test pattern '{}'==========", p);
  auto trie = aho_corasick::AhoCorasick();
  auto t    = trie.Local();

  // Pre-processing the pattern into pattern skeleton, then insert the split literals into AhoCorasick's trie
  auto skeleton = aho_corasick::Skeleton(p, plen, [&](aho_corasick::Token &tok) {
    auto literal = std::string(p + tok.start, tok.len) + '\0';
    trie.Insert(literal.data(), literal.size(), {0, tok.start, tok.len}, t);
    fmt::println("Insert literal '{}' into Aho-Corasick", literal);
  });
  if (skeleton.IsEmpty()) { return aho_corasick::Skeleton::SpecialMatchEmptyPattern(slen, p, plen); }

  // Building suffix & output links
  trie.BuildSuffixLink(1);

  // Start matching text
  ActiveMatch instance = {};
  auto iterate         = trie.StartIterativeParseText(s, slen);
  for (auto end_offset = 0UL; end_offset < slen; end_offset++) {
    auto ac_matchers = trie.ContinueParseText(iterate);
    auto &sket       = skeleton[instance.skeleton_idx];
    if (instance.underscore_left > 0) {
      // Prev sket has some suffix underscore, so we have to skip this much
      // TODO: We should only skip on a pattern basis, i.e., must check for pattern ID in the ac_matcher part below
      instance.underscore_left--;
      continue;
    }
    for (auto &match : ac_matchers) {
      // Must match within the current considerate pattern
      if (sket.Contain(match.pattern_index.start_pos)) {
        auto success = false;
        // Check if start a new matching instance
        if (sket.IsFirstLiteral(match.pattern_index.start_pos)) {
          if (sket.has_prefix_percent) {
            // With prefix percentage, the new 1st literal can start anywhere
            instance.match.emplace(match.text_start_pos, match.pattern_index.start_pos);
            success = true;
          } else {
            // Without prefix percentage, the new 1st literal must start exactly at the sket's first pos
            // This scenario only happens for the 1st literal and the literal is the prefix of the text & pattern
            assert(instance.skeleton_idx == 0);
            if (match.text_start_pos == 0) {
              instance.match.emplace(match.text_start_pos, match.pattern_index.start_pos);
              success = true;
            }
          }
        } else {
          // Otherwise, check if there is a previous literal that has the exact gap we are looking for
          for (auto &[prev_text_idx, prev_pat_pos] : instance.match) {
            if (sket.IsPreviousLiteral(prev_pat_pos, match.pattern_index.start_pos) &&
                match.text_start_pos >= prev_text_idx &&
                match.text_start_pos - prev_text_idx == match.pattern_index.start_pos - prev_pat_pos) {
              instance.match.erase({prev_text_idx, prev_pat_pos});
              instance.match.emplace(match.text_start_pos, match.pattern_index.start_pos);
              success = true;
            }
          }
        }
        // TODO: Handle case where we don't have a has_suffix_percent of the last literal, i.e., suffix match
        // Now, check if we just insert the last match of the sket
        if (success && match.pattern_index.start_pos == sket.last_literal_start_pos) {
          /**
           * We can only advance to the next sket if one of the following conditions is satisfied:
           * - Current sket is not the last one of the skeleton
           * - Current sket is the last one and has a suffix aho_corasick::PERCENTAGE
           * - Current sket is the last one, doesn't have a suffix aho_corasick::PERCENTAGE, and the AhoCorasick matcher
           * states that the current matching is the suffix of the queried text, including suffixed underscores
           */
          auto next_sket_index = instance.skeleton_idx + 1;
          if ((next_sket_index < skeleton.Size()) ||
              (next_sket_index >= skeleton.Size() && skeleton.Last().has_suffix_percent) ||
              (next_sket_index >= skeleton.Size() && !skeleton.Last().has_suffix_percent &&
               match.text_start_pos + match.pattern_index.keyword_len + sket.suffix_underscore_cnt == slen)) {
            instance.skeleton_idx++;
            instance.underscore_left = sket.suffix_underscore_cnt;
            instance.match.clear();
            if (instance.skeleton_idx >= skeleton.Size()) { return true; }
          }
        }
      }
    }
  }
  return false;
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
    auto try_pat = AhoCorasickMatchingOptimize(text.c_str(), text.size(), pat.c_str(), pat.size());
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
