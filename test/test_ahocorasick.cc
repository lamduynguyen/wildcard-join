#include "aho_corasick/aho_corasick.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

#include <string>
#include <string_view>

using namespace std::literals;

namespace aho_corasick {

TEST(TestAhoCorasick, SingleByteUnicode) {
  auto dataset =
    std::vector<std::u8string>{u8"he", u8"she", u8"his", u8"hers", u8"herself", u8"hero", u8"sheep", u8"eep"};
  auto trie = aho_corasick::AhoCorasick();

  // Insert dataset
  auto t   = trie.Local();
  auto idx = 0U;
  for (auto &keyword : dataset) {
    trie.Insert(reinterpret_cast<char *>(keyword.data()), keyword.size(), {idx++, 0}, t);
  }

  // Building suffix & output links
  trie.BuildSuffixLink(1);

  // ---- Root children ----
  auto root = trie.GetRoot();
  for (auto c : {u8'h', u8's', u8'e'}) {  // now also 'e' because of "eep"
    auto child = ART::N::getChild(static_cast<uint8_t>(c), root);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(ART::N::getSuffixLink(child), root);
  }

  // ---- "he" ----
  auto h  = ART::N::getChild(u8'h', root);
  auto he = ART::N::getChild(u8'e', h);
  ASSERT_NE(h, nullptr);
  ASSERT_NE(he, nullptr);
  EXPECT_EQ(ART::N::getSuffixLink(h), root);
  EXPECT_EQ(ART::N::getSuffixLink(he), ART::N::getChild(u8'e', root));

  // ---- "she" → suffix = "he" ----
  auto s   = ART::N::getChild(u8's', root);
  auto sh  = ART::N::getChild(u8'h', s);
  auto she = ART::N::getChild(u8'e', sh);
  ASSERT_NE(she, nullptr);
  EXPECT_EQ(ART::N::getSuffixLink(she), he);
  EXPECT_EQ(ART::N::getOutputLink(she), he);

  // ---- "hers" ----
  auto r  = ART::N::getChild(u8'r', he);
  auto rs = ART::N::getChild(u8's', r);
  ASSERT_NE(r, nullptr);
  ASSERT_NE(rs, nullptr);

  EXPECT_EQ(ART::N::getSuffixLink(r), root);
  EXPECT_EQ(ART::N::getSuffixLink(rs), s);
  EXPECT_EQ(ART::N::getOutputLink(rs), nullptr);

  // ---- "hero" ----
  auto her  = ART::N::getChild(u8'r', he);
  auto hero = ART::N::getChild(u8'o', her);
  ASSERT_NE(hero, nullptr);
  EXPECT_EQ(ART::N::getSuffixLink(hero), root);

  // ---- "eep" adds new suffix matches ----
  auto e_root = ART::N::getChild(u8'e', root);
  ASSERT_NE(e_root, nullptr);
  auto e_e = ART::N::getChild(u8'e', e_root);
  ASSERT_NE(e_e, nullptr);
  EXPECT_EQ(ART::N::getSuffixLink(e_e), e_root);

  // node for "ee" inside "sheep" → suffix = "e"
  auto e2 = ART::N::getChild(u8'e', she);
  ASSERT_NE(e2, nullptr);
  EXPECT_EQ(ART::N::getSuffixLink(e2), e_e);

  // suffix("ee") = "e"
  // suffix("e") is not terminal
  // output("e") = nullptr → so output("ee") = nullptr
  EXPECT_EQ(ART::N::getOutputLink(e2), nullptr);

  // ---- check final "eep" match: p-node has suffix = terminal("eep") ? ----
  auto p = ART::N::getChild(u8'p', e2);
  ASSERT_NE(p, nullptr);

  auto eep_terminal = ART::N::getChild(u8'p', e_e);
  ASSERT_NE(eep_terminal, nullptr);
  ASSERT_TRUE(eep_terminal->isTerminalNode());

  // p's suffix = p from pattern "eep"
  EXPECT_EQ(ART::N::getSuffixLink(p), eep_terminal);

  // and because suffix is terminal, output(p) = suffix(p)
  EXPECT_EQ(ART::N::getOutputLink(p), eep_terminal);

  // ---- Terminal nodes ----
  for (const auto &pat : dataset) {
    auto cur = root;
    for (char8_t c : pat) {  // iterate over char8_t
      cur = ART::N::getChild(static_cast<uint8_t>(c), cur);
      ASSERT_NE(cur, nullptr);
    }
    EXPECT_TRUE(cur->isTerminalNode());
    EXPECT_TRUE(ART::N::isLeaf(ART::N::getChild(NULL_TERMINATOR, cur)));
  }
}

TEST(TestAhoCorasick, MultiByteUnicode) {
  // ---- Dataset ----
  // Mostly ASCII, but some patterns include 'é' (2-byte UTF-8)
  auto dataset = std::vector<std::u8string>{
    u8"hé",       // é = 2 bytes
    u8"she",      // shares 'h','e' with "hé"
    u8"his",      // shares 'h','i','s'
    u8"hérs",     // shares 'h','é','r','s'
    u8"hérself",  // shares prefix with "hérs"
    u8"hero",     // ASCII, shares 'h','e','r'
    u8"sheep",    // ASCII + shared prefix "she"
    u8"ėep"       // ė = 2 bytes, shares "eep" with "sheep"
  };

  auto trie = aho_corasick::AhoCorasick();

  // ---- Insert dataset ----
  auto t   = trie.Local();
  auto idx = 0U;
  for (auto &keyword : dataset) {
    // Insert each pattern as bytes
    trie.Insert(reinterpret_cast<char *>(keyword.data()), keyword.size(), {idx++, 0}, t);
  }

  // ---- Build suffix & output links ----
  trie.BuildSuffixLink(1);

  // ---- Root children ----
  auto root = trie.GetRoot();
  ASSERT_EQ(root->getCount(), 3);
  for (auto c : {u8'h', u8's'}) {  // 'h', 's' are first letters of patterns
    auto child = ART::N::getChild(static_cast<uint8_t>(c), root);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(ART::N::getSuffixLink(child), root);
    ASSERT_TRUE(child->isLastByteOfCodePoint());
  }
  // 'ė' is a code point of two bytes [0xC4, 0x97], hence inserting this code point into trie
  //    creates a subtree of `root => 0xC4 => 0x97`
  auto first_child = ART::N::getChild(0xC4, root);
  ASSERT_NE(first_child, nullptr);
  ASSERT_EQ(first_child->getCount(), 1);
  ASSERT_FALSE(first_child->isLastByteOfCodePoint());

  first_child = ART::N::getChild(0x97, first_child);
  ASSERT_NE(first_child, nullptr);
  ASSERT_GE(first_child->getCount(), 1);
  ASSERT_TRUE(first_child->isLastByteOfCodePoint());

  // // ---- "hé" ----
  // auto h  = ART::N::getChild(static_cast<uint8_t>(u8'h'), root);
  // auto he = ART::N::getChild(static_cast<uint8_t>(u8'é'), h);
  // ASSERT_NE(h, nullptr);
  // ASSERT_NE(he, nullptr);
  // EXPECT_EQ(ART::N::getSuffixLink(h), root);

  // // ---- "she" → suffix = "he" ----
  // auto s   = ART::N::getChild(static_cast<uint8_t>(u8's'), root);
  // auto sh  = ART::N::getChild(static_cast<uint8_t>(u8'h'), s);
  // auto she = ART::N::getChild(static_cast<uint8_t>(u8'e'), sh);
  // ASSERT_NE(she, nullptr);
  // EXPECT_EQ(ART::N::getSuffixLink(she), he);
  // EXPECT_EQ(ART::N::getOutputLink(she), he);

  // // ---- "hérs" ----
  // auto r  = ART::N::getChild(static_cast<uint8_t>(u8'r'), he);
  // auto rs = ART::N::getChild(static_cast<uint8_t>(u8's'), r);
  // ASSERT_NE(r, nullptr);
  // ASSERT_NE(rs, nullptr);
  // EXPECT_EQ(ART::N::getSuffixLink(r), root);
  // EXPECT_EQ(ART::N::getSuffixLink(rs), s);
  // EXPECT_EQ(ART::N::getOutputLink(rs), nullptr);

  // // ---- "hero" ----
  // auto her  = ART::N::getChild(static_cast<uint8_t>(u8'r'), he);
  // auto hero = ART::N::getChild(static_cast<uint8_t>(u8'o'), her);
  // ASSERT_NE(hero, nullptr);
  // EXPECT_EQ(ART::N::getSuffixLink(hero), root);

  // // ---- "ėep" adds new suffix matches ----
  // auto ee_root = ART::N::getChild(static_cast<uint8_t>(u8'ė'), root);
  // ASSERT_NE(ee_root, nullptr);
  // auto e_e = ART::N::getChild(static_cast<uint8_t>(u8'e'), ee_root);
  // ASSERT_NE(e_e, nullptr);
  // EXPECT_EQ(ART::N::getSuffixLink(e_e), root);  // suffix of "ė" is root

  // // node for "ee" inside "sheep" → suffix = "e"
  // auto e2 = ART::N::getChild(static_cast<uint8_t>(u8'e'), she);
  // ASSERT_NE(e2, nullptr);
  // EXPECT_EQ(ART::N::getSuffixLink(e2), e_e);  // "ėe" ?

  // // ---- final "ėep" match ----
  // auto p = ART::N::getChild(static_cast<uint8_t>(u8'p'), e2);
  // ASSERT_NE(p, nullptr);

  // auto eep_terminal = ART::N::getChild(static_cast<uint8_t>(u8'p'), e_e);
  // ASSERT_NE(eep_terminal, nullptr);
  // ASSERT_TRUE(eep_terminal->isTerminalNode());

  // EXPECT_EQ(ART::N::getSuffixLink(p), eep_terminal);
  // EXPECT_EQ(ART::N::getOutputLink(p), eep_terminal);

  // // ---- Terminal nodes check ----
  // for (const auto &pat : dataset) {
  //   auto cur = root;
  //   std::string bytes_debug;
  //   for (char8_t c : pat) {
  //     cur = ART::N::getChild(static_cast<uint8_t>(c), cur);
  //     ASSERT_NE(cur, nullptr);
  //     bytes_debug += static_cast<char>(c);  // debug output
  //   }
  //   EXPECT_TRUE(cur->isTerminalNode()) << "Pattern not terminal: " << bytes_debug;
  //   EXPECT_TRUE(ART::N::isLeaf(ART::N::getChild(NULL_TERMINATOR, cur)));
  // }
}

}  // namespace aho_corasick

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
