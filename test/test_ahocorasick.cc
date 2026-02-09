#include "aho_corasick/aho_corasick.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

namespace aho_corasick {

TEST(TestAhoCorasick, SuffixLink) {
  auto dataset = std::vector<std::string>{"he", "she", "his", "hers", "herself", "hero", "sheep", "eep"};
  auto trie    = aho_corasick::AhoCorasick();

  // Insert dataset
  auto t   = trie.Local();
  auto idx = 0UL;
  for (auto &keyword : dataset) { trie.Insert(keyword.data(), keyword.size() + 1, {idx++, 0, keyword.size() + 1}, t); }

  // Building suffix & output links
  trie.BuildSuffixLink(1);

  // ---- Root children ----
  auto root = trie.GetRoot();
  for (char c : {'h', 's', 'e'}) {  // now also 'e' because of "eep"
    auto child = ART::N::getChild(c, root);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(ART::N::getSuffixLink(child), root);
  }

  // ---- "he" ----
  auto h  = ART::N::getChild('h', root);
  auto he = ART::N::getChild('e', h);
  ASSERT_NE(h, nullptr);
  ASSERT_NE(he, nullptr);
  EXPECT_EQ(ART::N::getSuffixLink(h), root);
  EXPECT_EQ(ART::N::getSuffixLink(he), ART::N::getChild('e', root));

  // ---- "she" → suffix = "he" ----
  auto s   = ART::N::getChild('s', root);
  auto sh  = ART::N::getChild('h', s);
  auto she = ART::N::getChild('e', sh);
  ASSERT_NE(she, nullptr);
  EXPECT_EQ(ART::N::getSuffixLink(she), he);
  EXPECT_EQ(ART::N::getOutputLink(she), he);

  // ---- "hers" ----
  auto r  = ART::N::getChild('r', he);
  auto rs = ART::N::getChild('s', r);
  ASSERT_NE(r, nullptr);
  ASSERT_NE(rs, nullptr);

  EXPECT_EQ(ART::N::getSuffixLink(r), root);
  EXPECT_EQ(ART::N::getSuffixLink(rs), s);
  EXPECT_EQ(ART::N::getOutputLink(rs), nullptr);

  // ---- "hero" ----
  auto her  = ART::N::getChild('r', he);
  auto hero = ART::N::getChild('o', her);
  ASSERT_NE(hero, nullptr);
  EXPECT_EQ(ART::N::getSuffixLink(hero), root);

  // ---- "eep" adds new suffix matches ----
  auto e_root = ART::N::getChild('e', root);
  ASSERT_NE(e_root, nullptr);
  auto e_e = ART::N::getChild('e', e_root);
  ASSERT_NE(e_root, nullptr);
  EXPECT_EQ(ART::N::getSuffixLink(e_e), e_root);

  // node for "ee" inside "sheep" → suffix = "e"
  auto e2 = ART::N::getChild('e', she);
  ASSERT_NE(e2, nullptr);
  EXPECT_EQ(ART::N::getSuffixLink(e2), e_e);

  // suffix("ee") = "e"
  // suffix("e") is not terminal
  // output("e") = nullptr → so output("ee") = nullptr
  EXPECT_EQ(ART::N::getOutputLink(e2), nullptr);

  // ---- check final "eep" match: p-node has suffix = terminal("eep") ? ----
  auto p = ART::N::getChild('p', e2);
  ASSERT_NE(p, nullptr);

  auto eep_terminal = ART::N::getChild('p', e_e);
  ASSERT_NE(eep_terminal, nullptr);
  ASSERT_TRUE(eep_terminal->isTerminalNode());

  // p's suffix = p from pattern "eep"
  EXPECT_EQ(ART::N::getSuffixLink(p), eep_terminal);

  // and because suffix is terminal, output(p) = suffix(p)
  EXPECT_EQ(ART::N::getOutputLink(p), eep_terminal);

  // ---- Terminal nodes ----
  for (const auto &pat : dataset) {
    auto cur = root;
    for (char c : pat) {
      cur = ART::N::getChild((uint8_t)c, cur);
      ASSERT_NE(cur, nullptr);
    }
    EXPECT_TRUE(cur->isTerminalNode());
    EXPECT_TRUE(ART::N::isLeaf(ART::N::getChild(NULL_TERMINATOR, cur)));
  }
}

}  // namespace aho_corasick

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
