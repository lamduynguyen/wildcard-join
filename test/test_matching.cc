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

TEST(TestMatching, DuckDB) {
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

  for (auto &[pat, result] : tests) {
    auto try_pat = DuckDBMatching(text.c_str(), text.size(), pat.c_str(), pat.size());
    if (try_pat != result) { std::cout << "try pattern: '" << pat << "' return wrong result" << std::endl; }
    EXPECT_EQ(try_pat, result);
  }
}

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
