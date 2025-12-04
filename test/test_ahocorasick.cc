#include "aho_corasick/aho_corasick.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

namespace aho_corasick {

TEST(TestArt, SuffixLink) {
  auto dataset = std::vector<std::string>{"he", "she", "his", "hers", "herself", "hero", "sheep", "eep"};
  for (auto &key : dataset) { key += '\0'; }
  auto trie = aho_corasick::AhoCorasick();

  // Insert dataset
  Key key;
  auto t   = trie.Local();
  auto idx = 0UL;
  for (auto &keyword : dataset) { trie.Insert(keyword.data(), keyword.size(), {idx++, 0}, t); }

  // Building suffix & output links
  trie.BuildSuffixLink(1);

  // ---- Root children ----
  auto root = trie.GetRoot();
  for (char c : {'h', 's', 'e'}) {  // now also 'e' because of "eep"
    auto child = ART::N::getChild(c, root);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->getSuffixLink(), root);
  }

  // ---- "he" ----
  auto h  = ART::N::getChild('h', root);
  auto he = ART::N::getChild('e', h);
  ASSERT_NE(h, nullptr);
  ASSERT_NE(he, nullptr);
  EXPECT_EQ(h->getSuffixLink(), root);
  EXPECT_EQ(he->getSuffixLink(), ART::N::getChild('e', root));

  // ---- "she" → suffix = "he" ----
  auto s   = ART::N::getChild('s', root);
  auto sh  = ART::N::getChild('h', s);
  auto she = ART::N::getChild('e', sh);
  ASSERT_NE(she, nullptr);
  EXPECT_EQ(she->getSuffixLink(), he);
  EXPECT_EQ(she->getOutputLink(), he);

  // ---- "hers" ----
  auto r  = ART::N::getChild('r', he);
  auto rs = ART::N::getChild('s', r);
  ASSERT_NE(r, nullptr);
  ASSERT_NE(rs, nullptr);

  EXPECT_EQ(r->getSuffixLink(), root);
  EXPECT_EQ(rs->getSuffixLink(), s);
  EXPECT_EQ(rs->getOutputLink(), nullptr);

  // ---- "hero" ----
  auto her  = ART::N::getChild('r', he);
  auto hero = ART::N::getChild('o', her);
  ASSERT_NE(hero, nullptr);
  EXPECT_EQ(hero->getSuffixLink(), root);

  // ---- "eep" adds new suffix matches ----
  auto e_root = ART::N::getChild('e', root);
  ASSERT_NE(e_root, nullptr);
  auto e_e = ART::N::getChild('e', e_root);
  ASSERT_NE(e_root, nullptr);
  EXPECT_EQ(e_e->getSuffixLink(), e_root);

  // node for "ee" inside "sheep" → suffix = "e"
  auto e2 = ART::N::getChild('e', she);
  ASSERT_NE(e2, nullptr);
  EXPECT_EQ(e2->getSuffixLink(), e_e);

  // suffix("ee") = "e"
  // suffix("e") is not terminal
  // output("e") = nullptr → so output("ee") = nullptr
  EXPECT_EQ(e2->getOutputLink(), nullptr);

  // ---- check final "eep" match: p-node has suffix = terminal("eep") ? ----
  auto p = ART::N::getChild('p', e2);
  ASSERT_NE(p, nullptr);

  auto eep_terminal = ART::N::getChild('p', e_e);
  ASSERT_NE(eep_terminal, nullptr);
  ASSERT_TRUE(eep_terminal->isTerminalNode());

  // p's suffix = p from pattern "eep"
  EXPECT_EQ(p->getSuffixLink(), eep_terminal);

  // and because suffix is terminal, output(p) = suffix(p)
  EXPECT_EQ(p->getOutputLink(), eep_terminal);

  // ---- Terminal nodes ----
  for (const auto &pat_full : dataset) {
    const auto pat = pat_full.substr(0, pat_full.size() - 1);
    ART::N *cur    = root;
    for (char c : pat) {
      cur = ART::N::getChild((uint8_t)c, cur);
      ASSERT_NE(cur, nullptr);
    }
    EXPECT_TRUE(cur->isTerminalNode());
    EXPECT_TRUE(ART::N::isLeaf(ART::N::getChild(ART::NULL_TERMINATOR, cur)));
  }
}

}  // namespace aho_corasick

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
