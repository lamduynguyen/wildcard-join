// Guards TextParserIterator::ResetText.
//
// The probe path used to build one Matcher per pattern for every text row.
// Now the iterator is built once and ResetText puts it back to its initial
// state between rows, touching only the matchers that the previous row dirtied.
// If that reset misses anything, results start depending on which rows ran
// before, which is the worst kind of bug to find later: the answer is right in
// isolation and wrong in a batch.
//
// So every case here is checked three ways.
//   1. A fresh iterator per row, which is the old behaviour.
//   2. One iterator reused across the rows in order.
//   3. The same reused iterator run over the rows a second time, and again
//      with the row order reversed.
// All four must agree with each other and with the recursive reference
// matcher.

#include "matcher.h"

#include "aho_corasick/aho_corasick.h"
#include "aho_corasick/parser.h"
#include "aho_corasick/skeleton.h"
#include "fmt/format.h"
#include "gtest/gtest.h"

#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

using aho_corasick::AhoCorasick;
using aho_corasick::PatternAnalyzer;
using aho_corasick::TextParserIterator;

// Owns a compiled pattern set so several probe strategies can share it.
struct BuildSide {
  AhoCorasick trie;
  std::vector<std::string> patterns;
  std::unique_ptr<PatternAnalyzer> analyzer;

  explicit BuildSide(std::vector<std::string> pats) : patterns(std::move(pats)) {
    analyzer = std::make_unique<PatternAnalyzer>(patterns, "", trie);
    trie.BuildSuffixLink();
  }
};

// The wildcard-only short circuit lives in the caller in every existing probe
// loop, so it lives here too rather than inside the iterator.
void ApplyWildcardOnly(BuildSide &b, const std::string &text, std::vector<bool> &result) {
  for (auto idx = 0U; idx < b.patterns.size(); idx++) {
    const auto &sket = b.analyzer->GetSkeleton(idx);
    if (sket.IsEmpty() || sket.OnlyWildcard()) {
      result[idx] = aho_corasick::Skeleton::SpecialMatchEmptyPattern(
        b.patterns[idx].data(), static_cast<u32>(b.patterns[idx].size()), text.data(), text.size());
    }
  }
}

void Drive(BuildSide &b, TextParserIterator &iter, const std::string &text, std::vector<bool> &result) {
  std::fill(result.begin(), result.end(), false);
  ApplyWildcardOnly(b, text, result);
  iter.ResetText(text.data(), text.size());
  while (iter.CanAdvanceOneCodePoint()) {
    iter.IterateOneCodePoint(result);
    iter.ProcessDelayedMatching();
  }
}

auto ProbeFresh(BuildSide &b, const std::string &text) -> std::vector<bool> {
  std::vector<bool> result(b.patterns.size(), false);
  ApplyWildcardOnly(b, text, result);
  TextParserIterator iter(text.data(), text.size(), b.analyzer.get(), &b.trie);
  while (iter.CanAdvanceOneCodePoint()) {
    iter.IterateOneCodePoint(result);
    iter.ProcessDelayedMatching();
  }
  return result;
}

auto Reference(const std::vector<std::string> &patterns, const std::string &text) -> std::vector<bool> {
  std::vector<bool> out(patterns.size(), false);
  for (auto idx = 0U; idx < patterns.size(); idx++) {
    out[idx] = DuckDBMatching(text.data(), text.size(), patterns[idx].data(), patterns[idx].size());
  }
  return out;
}

void CheckReuse(const std::vector<std::string> &patterns, const std::vector<std::string> &texts) {
  BuildSide b(patterns);

  std::vector<std::vector<bool>> fresh;
  fresh.reserve(texts.size());
  for (const auto &t : texts) {
    fresh.push_back(ProbeFresh(b, t));
    EXPECT_EQ(fresh.back(), Reference(patterns, t))
      << "fresh iterator disagrees with the reference on text '" << t << "'";
  }

  TextParserIterator iter(b.analyzer.get(), &b.trie);
  std::vector<bool> result(patterns.size(), false);

  for (auto pass = 0; pass < 2; pass++) {
    for (auto ti = 0U; ti < texts.size(); ti++) {
      Drive(b, iter, texts[ti], result);
      EXPECT_EQ(result, fresh[ti]) << "reused iterator, pass " << pass << ", text '" << texts[ti] << "'";
    }
  }

  for (auto ti = texts.size(); ti-- > 0;) {
    Drive(b, iter, texts[ti], result);
    EXPECT_EQ(result, fresh[ti]) << "reused iterator, reverse order, text '" << texts[ti] << "'";
  }
}

}  // namespace

// Multi-segment patterns are the ones that leave a matcher parked on a later
// segment when the text runs out, so they are the ones a missed reset breaks.
TEST(TestReuse, MultiSegmentPatternsAcrossRows) {
  std::vector<std::string> patterns = {
    "%alpha%beta%", "%beta%alpha%", "%alpha%", "%beta%", "alpha%", "%beta", "%al_ha%", "%a%b%c%",
  };
  std::vector<std::string> texts = {
    "alpha and then beta",   // matches the first, and parks nothing
    "beta only here",        // the previous row left segment 1 of "%alpha%beta%" armed
    "alpha only here",       // parks "%alpha%beta%" on its second segment, no beta follows
    "beta only here",        // must not inherit the parked alpha from the row above
    "alpha",                 //
    "beta",                  //
    "nothing relevant here"  //
  };
  CheckReuse(patterns, texts);
}

// A row whose delayed underscore queue still has entries when the text ends.
// Those entries name a pattern and a code point index, and both are stale for
// the next row.
TEST(TestReuse, DelayedQueueLeftoversDoNotLeak) {
  std::vector<std::string> patterns = {
    "%ab_cd%", "%ab__cd%", "%ab___cd%", "%ab_cd", "ab_cd%",
  };
  std::vector<std::string> texts = {
    "xxab",     // schedules the cd lookahead, then the text ends
    "cdxxxx",   // the stale schedule from above would fire here
    "abxcd",    //
    "ab",       //
    "abxxcd",   //
    "cd",       //
    "abxxxxcd"  //
  };
  CheckReuse(patterns, texts);
}

// Same thing on multi-byte code points, where the byte offset and the code
// point index diverge and the delayed queue actually earns its keep.
TEST(TestReuse, UnicodeAcrossRows) {
  std::vector<std::string> patterns = {
    "%\xf0\x9f\x98\x80_\xf0\x9f\x8d\x95%",  // grinning face, one code point, pizza
    "%\xf0\x9f\x98\x80%",                   // grinning face
    "%\xf0\x9f\x8d\x95%",                   // pizza
    "%\xc3\xa4_\xc3\xb6%",                  // a umlaut, one code point, o umlaut
  };
  std::vector<std::string> texts = {
    "\xf0\x9f\x98\x80",                                  // just the grinning face, arms the lookahead
    "\xf0\x9f\x8d\x95",                                  // just the pizza
    "\xf0\x9f\x98\x80\xf0\x9f\x90\x8d\xf0\x9f\x8d\x95",  // face, snake, pizza
    "\xc3\xa4x\xc3\xb6",                                 //
    "\xc3\xa4",                                          //
    "\xc3\xb6",                                          //
    "\xf0\x9f\x98\x80x\xf0\x9f\x8d\x95"                  // face, x, pizza
  };
  CheckReuse(patterns, texts);
}

// The wildcard-only patterns get a zero capacity Matcher that is never dirty.
// Mixing them in makes sure the reset loop is not indexing by the wrong id.
TEST(TestReuse, WildcardOnlyPatternsMixedIn) {
  std::vector<std::string> patterns = {
    "%", "%needle%", "%%", "_", "%needle", "__", "needle%",
  };
  std::vector<std::string> texts = {
    "needle", "a", "ab", "haystack with a needle in it", "", "needle at the front", "at the back needle",
  };
  CheckReuse(patterns, texts);
}

// The hand written cases above cover the states I could reason my way to.
// They are not enough on their own: dropping the delayed queue reset leaves
// every one of them green, because a stale queue entry only changes an answer
// when its pattern id, its code point deadline and its alignment all happen to
// line up on the next row. Random rows find that on their own.
//
// This compares reused against fresh only, not against the recursive matcher.
// A disagreement with the reference on a random pattern would be an engine
// bug, which is a different piece of work. What is being asserted here is the
// narrow property this change is responsible for: the answer for a row must
// not depend on the rows that ran before it.
TEST(TestReuse, RandomizedRowOrderIndependence) {
  // Two letter alphabet on purpose. Long alphabets make almost every pattern
  // fail on the first literal, and nothing interesting is left in the matcher.
  static constexpr std::string_view PATTERN_ALPHABET = "ab%_";
  static constexpr std::string_view TEXT_ALPHABET    = "ab";

  std::mt19937 rng(0x5EED);
  std::uniform_int_distribution<size_t> pat_len(1, 7);
  std::uniform_int_distribution<size_t> txt_len(0, 9);
  std::uniform_int_distribution<size_t> pat_chr(0, PATTERN_ALPHABET.size() - 1);
  std::uniform_int_distribution<size_t> txt_chr(0, TEXT_ALPHABET.size() - 1);

  std::vector<std::string> patterns;
  patterns.reserve(200);
  for (auto i = 0U; i < 200; i++) {
    std::string p(pat_len(rng), '\0');
    for (auto &c : p) { c = PATTERN_ALPHABET[pat_chr(rng)]; }
    patterns.push_back(std::move(p));
  }

  std::vector<std::string> texts;
  texts.reserve(500);
  for (auto i = 0U; i < 500; i++) {
    std::string t(txt_len(rng), '\0');
    for (auto &c : t) { c = TEXT_ALPHABET[txt_chr(rng)]; }
    texts.push_back(std::move(t));
  }

  BuildSide b(patterns);

  std::vector<std::vector<bool>> fresh;
  fresh.reserve(texts.size());
  for (const auto &t : texts) { fresh.push_back(ProbeFresh(b, t)); }

  TextParserIterator iter(b.analyzer.get(), &b.trie);
  std::vector<bool> result(patterns.size(), false);
  for (auto ti = 0U; ti < texts.size(); ti++) {
    Drive(b, iter, texts[ti], result);
    ASSERT_EQ(result, fresh[ti]) << "row " << ti << ", text '" << texts[ti] << "'";
  }
  for (auto ti = texts.size(); ti-- > 0;) {
    Drive(b, iter, texts[ti], result);
    ASSERT_EQ(result, fresh[ti]) << "reverse order, row " << ti << ", text '" << texts[ti] << "'";
  }
}

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
