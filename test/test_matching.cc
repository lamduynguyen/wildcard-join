#include "aho_corasick/aho_corasick.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

#include <cstdlib>
#include <deque>
#include <queue>
#include <ranges>
#include <vector>

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

  static auto IsDelim(char c) { return c == UNDERSCORE || c == PERCENTAGE; };

  static auto NextToken(const char *s, size_t slen, std::size_t &pos) -> Token {
    auto prev_pos = pos;
    while (pos < slen && IsDelim(s[pos])) { pos++; }
    if (pos >= slen) {
      // no more tokens; resetting pos back to suffix processing
      pos = prev_pos;
      return {{}, std::string_view::npos};
    }
    std::size_t start = pos;
    while (pos < slen && !IsDelim(s[pos])) { pos++; }
    return {std::string(s + start, pos - start) + '\0', start};
  };

  static auto NextTokenIncludeUnderscore(const char *s, size_t slen, std::size_t &pos) -> Token {
    auto prev_pos = pos;
    while (pos < slen && s[pos] == PERCENTAGE) { pos++; }
    if (pos >= slen) {
      // no more tokens; resetting pos back to suffix processing
      pos = prev_pos;
      return {{}, std::string_view::npos};
    }
    std::size_t start = pos;
    while (pos < slen && s[pos] != PERCENTAGE) { pos++; }
    return {std::string(s + start, pos - start), start};  // we don't need to append '\0' here
  };
};

bool AhoCorasickMatchingGreedy(const char *s, size_t slen, const char *p, size_t plen) {
  // Aho-Corasick env
  auto trie = aho_corasick::AhoCorasick();
  auto t    = trie.Local();

  // Split patterns into keywords, separated by % and _, and then insert into AhoCorasick's trie
  size_t pos = 0;
  while (true) {
    auto tok = Token::NextToken(p, plen, pos);
    if (tok.start == std::string::npos) { break; }
    assert(pos >= tok.start);
    trie.Insert(tok.view.data(), tok.view.size(), {0, tok.start, tok.view.size() - 1}, t);
  }

  // Building suffix & output links
  trie.BuildSuffixLink(1);

  // Get substring matcher info from
  auto ac_matchers    = trie.ParseText(s, slen);
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
      auto tmp         = aho_corasick::PatternIndexType(0, p_idx, 0);
      auto output_tp   = aho_corasick::MatchingOutputType(tmp, s_idx);
      auto matcher_ptr = ac_matchers.find(output_tp);
      if (p_idx < plen && matcher_ptr != ac_matchers.end()) {
        assert(std::memcmp(&s[s_idx], &p[p_idx], matcher_ptr->pattern_index.keyword_len) == 0);
        p_idx += matcher_ptr->pattern_index.keyword_len;
        s_idx += matcher_ptr->pattern_index.keyword_len;
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

struct MatchSkeleton {
  std::unordered_map<u64, u64> subset;  // Map from literal's start pos to that of previous literal
  u64 pattern_start_pos;
  u64 pattern_end_pos;
  u64 start_pos_of_last_literal;
  u64 suffix_underscore_cnt;
  bool has_prefix_percent;
  bool has_suffix_percent;
};

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

  // Split patterns into keywords, separated by % and _, and then insert into AhoCorasick's trie
  std::vector<MatchSkeleton> skeleton;
  size_t last_end_pos = 0;
  while (true) {
    MatchSkeleton match = {};

    // Extract tokens by PERCENTAGE only
    auto prev_pos = last_end_pos;
    auto tok      = Token::NextTokenIncludeUnderscore(p, plen, last_end_pos);
    if (tok.start == std::string::npos) { break; }
    if (prev_pos < tok.start) { match.has_prefix_percent = true; };

    // Build between-PERCENTAGE skeleton based on the extracted token -- p[tok.start : last_end_pos]
    auto prev_start         = 0UL;
    match.pattern_start_pos = tok.start;
    match.pattern_end_pos   = last_end_pos;
    auto pos                = tok.start;
    for (auto pos = tok.start; pos < last_end_pos;) {
      auto literal = Token::NextToken(p, plen, pos);
      if (literal.start == std::string::npos) {
        // this means we have a suffix _ scenario
        match.suffix_underscore_cnt = last_end_pos - pos;
        break;
      }
      if (prev_start > 0) { match.subset.emplace(literal.start, prev_start); }
      prev_start                      = literal.start;
      match.start_pos_of_last_literal = literal.start;
      trie.Insert(literal.view.data(), literal.view.size(), {0, literal.start, literal.view.size() - 1}, t);
      fmt::println("Insert literal '{}' into Aho-Corasick", literal.view);
    }
    skeleton.emplace_back(std::move(match));
  }
  // Special case: There is no token in the pattern
  if (skeleton.empty()) {
    // In this case, there are only % and _.
    // Count how many _ and check if slen >= number of underscores
    auto underscore_cnt = 0UL;
    auto has_percent    = false;
    for (auto idx = 0UL; idx < plen; idx++) {
      if (p[idx] == UNDERSCORE) {
        underscore_cnt++;
      } else if (p[idx] == PERCENTAGE) {
        has_percent = true;
      }
    }
    return slen >= underscore_cnt && has_percent;
  }

  // We have the skeleton. Preprocessing its information
  if (last_end_pos != plen) { skeleton.back().has_suffix_percent = true; }
  for (int idx = skeleton.size() - 2; idx >= 0; idx--) {
    skeleton[idx].has_suffix_percent = skeleton[idx + 1].has_prefix_percent;
  }
  assert(!skeleton.empty());

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
      if (match.pattern_index.start_pos >= sket.pattern_start_pos &&
          match.pattern_index.start_pos <= sket.pattern_end_pos) {
        auto success = false;
        // Check if start a new matching instance
        if (match.pattern_index.start_pos == sket.pattern_start_pos) {  // is the 1st literal of the current sket
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
            if (prev_pat_pos == sket.subset[match.pattern_index.start_pos] && match.text_start_pos >= prev_text_idx &&
                match.text_start_pos - prev_text_idx == match.pattern_index.start_pos - prev_pat_pos) {
              instance.match.erase({prev_text_idx, prev_pat_pos});
              instance.match.emplace(match.text_start_pos, match.pattern_index.start_pos);
              success = true;
            }
          }
        }
        // TODO: Handle case where we don't have a has_suffix_percent of the last literal, i.e., suffix match
        // Now, check if we just insert the last match of the sket
        if (success && match.pattern_index.start_pos == sket.start_pos_of_last_literal) {
          /**
           * We can only advance to the next sket if one of the following conditions is satisfied:
           * - Current sket is not the last one of the skeleton
           * - Current sket is the last one and has a suffix PERCENTAGE
           * - Current sket is the last one, doesn't have a suffix PERCENTAGE, and the AhoCorasick matcher states that
           *   the current matching is the suffix of the queried text, including suffixed underscores
           */
          auto next_sket_index = instance.skeleton_idx + 1;
          if ((next_sket_index < skeleton.size()) ||
              (next_sket_index >= skeleton.size() && skeleton.back().has_suffix_percent) ||
              (next_sket_index >= skeleton.size() && !skeleton.back().has_suffix_percent &&
               match.text_start_pos + match.pattern_index.keyword_len + sket.suffix_underscore_cnt == slen)) {
            instance.skeleton_idx++;
            instance.underscore_left = sket.suffix_underscore_cnt;
            instance.match.clear();
            if (instance.skeleton_idx >= skeleton.size()) { return true; }
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
    auto try_pat = AhoCorasickMatchingGreedy(text.c_str(), text.size(), pat.c_str(), pat.size());
    if (try_pat != result) {
      std::cout << "AhoCorasick: evaluate pattern: '" << pat << "' return wrong result" << std::endl;
    }
    EXPECT_EQ(try_pat, result);
  }

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
