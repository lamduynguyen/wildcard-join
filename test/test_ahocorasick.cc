#include "aho_corasick/aho_corasick.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace std::literals;

namespace aho_corasick {

TEST(TestAhoCorasick, SingleByteUnicode) {
  auto dataset =
    std::vector<std::u8string>{u8"he", u8"she", u8"his", u8"hers", u8"herself", u8"hero", u8"sheep", u8"eep"};
  auto trie = aho_corasick::AhoCorasick();

  // Insert dataset
  auto idx = 0U;
  for (auto &keyword : dataset) { trie.Insert(reinterpret_cast<char *>(keyword.data()), keyword.size(), {idx++, 0}); }

  // Building suffix & output links
  trie.BuildSuffixLink();

  // ---- Root children ----
  auto root = trie.GetRoot();
  for (auto c : {u8'h', u8's', u8'e'}) {  // now also 'e' because of "eep"
    auto child = root->getChild(static_cast<uint8_t>(c));
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->getSuffixLink(), root);
  }

  // ---- "he" ----
  auto h  = root->getChild(u8'h');
  auto he = h->getChild(u8'e');
  ASSERT_NE(h, nullptr);
  ASSERT_NE(he, nullptr);
  EXPECT_EQ(h->getSuffixLink(), root);
  EXPECT_EQ(he->getSuffixLink(), root->getChild(u8'e'));

  // ---- "she" → suffix = "he" ----
  auto s   = root->getChild(u8's');
  auto sh  = s->getChild(u8'h');
  auto she = sh->getChild(u8'e');
  ASSERT_NE(she, nullptr);
  EXPECT_EQ(she->getSuffixLink(), he);
  EXPECT_EQ(she->getOutputLink(), he);

  // ---- "hers" ----
  auto r  = he->getChild(u8'r');
  auto rs = r->getChild(u8's');
  ASSERT_NE(r, nullptr);
  ASSERT_NE(rs, nullptr);

  EXPECT_EQ(r->getSuffixLink(), root);
  EXPECT_EQ(rs->getSuffixLink(), s);
  EXPECT_EQ(rs->getOutputLink(), nullptr);

  // ---- "hero" ----
  auto her  = he->getChild(u8'r');
  auto hero = her->getChild(u8'o');
  ASSERT_NE(hero, nullptr);
  EXPECT_EQ(hero->getSuffixLink(), root);

  // ---- "eep" adds new suffix matches ----
  auto e_root = root->getChild(u8'e');
  ASSERT_NE(e_root, nullptr);
  auto e_e = e_root->getChild(u8'e');
  ASSERT_NE(e_e, nullptr);
  EXPECT_EQ(e_e->getSuffixLink(), e_root);

  // node for "ee" inside "sheep" → suffix = "e"
  auto e2 = she->getChild(u8'e');
  ASSERT_NE(e2, nullptr);
  EXPECT_EQ(e2->getSuffixLink(), e_e);

  // suffix("ee") = "e"
  // suffix("e") is not terminal
  // output("e") = nullptr → so output("ee") = nullptr
  EXPECT_EQ(e2->getOutputLink(), nullptr);

  // ---- check final "eep" match: p-node has suffix = terminal("eep") ? ----
  auto p = e2->getChild(u8'p');
  ASSERT_NE(p, nullptr);

  auto eep_terminal = e_e->getChild(u8'p');
  ASSERT_NE(eep_terminal, nullptr);
  ASSERT_TRUE(eep_terminal->isTerminalNode());

  // p's suffix = p from pattern "eep"
  EXPECT_EQ(p->getSuffixLink(), eep_terminal);

  // and because suffix is terminal, output(p) = suffix(p)
  EXPECT_EQ(p->getOutputLink(), eep_terminal);

  // ---- Terminal nodes ----
  for (const auto &pat : dataset) {
    auto cur = root;
    for (const char8_t c : pat) {  // iterate over char8_t
      cur = cur->getChild(static_cast<uint8_t>(c));
      ASSERT_NE(cur, nullptr);
    }
    EXPECT_TRUE(cur->isTerminalNode());
    EXPECT_TRUE(ART::N256::isLeaf(cur->getChild(NULL_TERMINATOR)));
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
    u8"héro",     // shares 'h','é','r'
    u8"sheep",    // ASCII + shared prefix "she"
    u8"eep",      // ASCII variant of "ėep"
    u8"ėep",      // ė = 2 bytes, shares "ep" with "sheep"

    // ---- new patterns to create meaningful output links ----
    u8"self",  // suffix of "hérself" and "herself" (if added)
    u8"rs",    // suffix of "hérs" (creates output link from hérs/r->s)
    u8"ero"    // suffix of "hero" (creates output link from hero/o)
  };

  // ---- Insert dataset ----
  auto trie = aho_corasick::AhoCorasick();
  auto idx  = 0U;
  for (auto &keyword : dataset) {
    // Insert each pattern as bytes
    trie.Insert(reinterpret_cast<char *>(keyword.data()), keyword.size(), {idx++, 0});
  }

  // ---- Build suffix & output links ----
  trie.BuildSuffixLink();

  // ---- Root children ----
  auto root = trie.GetRoot();
  ASSERT_EQ(root->getCount(), 5);  // ['h', 's', 'e', 'r', 1st byte of 'ė']
  for (auto c : {u8'h', u8's'}) {  // 'h', 's' are first letters of patterns
    auto child = root->getChild(static_cast<uint8_t>(c));
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->getSuffixLink(), root);
    ASSERT_TRUE(child->isLastByteOfCodePoint());
  }
  // 'ė' is a code point of two bytes [0xC4, 0x97], hence inserting this code point into trie
  //    creates a subtree of `root => 0xC4 => 0x97`
  auto first_child = root->getChild(0xC4);
  ASSERT_NE(first_child, nullptr);
  ASSERT_EQ(first_child->getCount(), 1);
  ASSERT_FALSE(first_child->isTerminalNode());
  ASSERT_FALSE(first_child->isLastByteOfCodePoint());

  first_child = first_child->getChild(0x97);
  ASSERT_NE(first_child, nullptr);
  ASSERT_GE(first_child->getCount(), 1);
  ASSERT_FALSE(first_child->isTerminalNode());
  ASSERT_TRUE(first_child->isLastByteOfCodePoint());

  // ---- "hé" ----
  auto h = root->getChild(static_cast<uint8_t>(u8'h'));
  ASSERT_NE(h, nullptr);
  ASSERT_TRUE(h->isLastByteOfCodePoint());
  EXPECT_EQ(h->getSuffixLink(), root);

  // Iterate over bytes of 'é' (0xC3 0xA9)
  const char8_t e_bytes[] = {0xC3, 0xA9};
  auto cur                = h;
  for (size_t i = 0; i < sizeof(e_bytes); ++i) {
    cur = cur->getChild(e_bytes[i]);
    ASSERT_NE(cur, nullptr);

    // Check the last-byte flag
    if (i == sizeof(e_bytes) - 1) {
      EXPECT_TRUE(cur->isLastByteOfCodePoint()) << "Byte index " << i << " should be last byte of 'é'";
    } else {
      EXPECT_FALSE(cur->isLastByteOfCodePoint()) << "Byte index " << i << " should NOT be last byte of 'é'";
    }
  }
  auto he_prime = cur;
  ASSERT_TRUE(he_prime->isLastByteOfCodePoint());

  // ---- "she" → suffix = "he" ----
  auto s   = root->getChild(static_cast<uint8_t>(u8's'));
  auto sh  = s->getChild(static_cast<uint8_t>(u8'h'));
  auto she = sh->getChild(static_cast<uint8_t>(u8'e'));
  ASSERT_NE(she, nullptr);
  ASSERT_TRUE(she->isLastByteOfCodePoint());
  ASSERT_EQ(she->getCount(), 2);  // Resembles both 'she' and 'sheep'

  // ---- "rs" ----
  auto r_node      = root->getChild(static_cast<uint8_t>(u8'r'));    // 'r' after h+é
  auto rs_new_node = r_node->getChild(static_cast<uint8_t>(u8's'));  // 's'
  ASSERT_NE(rs_new_node, nullptr);
  ASSERT_TRUE(rs_new_node->isLastByteOfCodePoint());

  // ---- "hérs" ----
  auto her_prime  = he_prime->getChild(static_cast<uint8_t>(u8'r'));
  auto hers_prime = her_prime->getChild(static_cast<uint8_t>(u8's'));  // 'hérs' here
  ASSERT_NE(her_prime, nullptr);
  ASSERT_NE(hers_prime, nullptr);
  EXPECT_EQ(her_prime->getSuffixLink(), r_node);
  EXPECT_EQ(hers_prime->getSuffixLink(), rs_new_node);
  EXPECT_EQ(hers_prime->getOutputLink(), rs_new_node);

  // ---- "self" ----
  auto se_node   = s->getChild(static_cast<uint8_t>(u8'e'));         // 'e' after rs
  auto sel_node  = se_node->getChild(static_cast<uint8_t>(u8'l'));   // 'l'
  auto self_node = sel_node->getChild(static_cast<uint8_t>(u8'f'));  // 'f' -- till here got 'self'
  ASSERT_NE(self_node, nullptr);

  // ---- "hérself" → suffix = "self" ----
  auto rse_prime   = hers_prime->getChild(static_cast<uint8_t>(u8'e'));
  auto rsel_prime  = rse_prime->getChild(static_cast<uint8_t>(u8'l'));
  auto rself_prime = rsel_prime->getChild(static_cast<uint8_t>(u8'f'));
  ASSERT_NE(rself_prime, nullptr);
  EXPECT_EQ(rself_prime->getSuffixLink(), self_node);  // 'self' is suffix link of 'hérself'
  EXPECT_EQ(rself_prime->getOutputLink(), self_node);  // 'self' is also output link of 'hérself'

  // ---- "ero" ----
  auto e_node   = root->getChild(static_cast<uint8_t>(u8'e'));
  auto er_node  = e_node->getChild(static_cast<uint8_t>(u8'r'));   // 'r' after 'e'
  auto ero_node = er_node->getChild(static_cast<uint8_t>(u8'o'));  // 'o'
  ASSERT_NE(ero_node, nullptr);

  // ---- "héro" ----
  auto hero_prime = her_prime->getChild(static_cast<uint8_t>(u8'o'));
  ASSERT_NE(hero_prime, nullptr);
  EXPECT_EQ(hero_prime->getSuffixLink(), root);
  EXPECT_EQ(hero_prime->getOutputLink(), nullptr);

  // ---- "hero" ----
  auto he = h->getChild(static_cast<uint8_t>(u8'e'));
  ASSERT_NE(he, nullptr);
  auto her  = he->getChild(static_cast<uint8_t>(u8'r'));
  auto hero = her->getChild(static_cast<uint8_t>(u8'o'));
  ASSERT_NE(hero, nullptr);
  EXPECT_EQ(hero->getSuffixLink(), ero_node);

  // ---- "ėep" adds new suffix matches ----
  auto ee_root                = root;
  const uint8_t e_dot_bytes[] = {0xC4, 0x97};
  for (size_t i = 0; i < sizeof(e_dot_bytes); ++i) {
    ee_root = ee_root->getChild(e_dot_bytes[i]);
    ASSERT_NE(ee_root, nullptr);

    // Check the last-byte flag
    if (i == sizeof(e_dot_bytes) - 1) {
      EXPECT_TRUE(ee_root->isLastByteOfCodePoint()) << "Byte index " << i << " should be last byte of 'ė'";
    } else {
      EXPECT_FALSE(ee_root->isLastByteOfCodePoint()) << "Byte index " << i << " should NOT be last byte of 'ė'";
    }
  }

  // ---- "eep" adds new suffix matches ----
  auto e   = root->getChild(static_cast<uint8_t>(u8'e'));
  auto ee  = e->getChild(static_cast<uint8_t>(u8'e'));
  auto eep = ee->getChild(static_cast<uint8_t>(u8'p'));

  // ---- "sheep" → suffix = "eep" ----
  auto shee  = she->getChild(static_cast<uint8_t>(u8'e'));
  auto sheep = shee->getChild(static_cast<uint8_t>(u8'p'));
  ASSERT_NE(sheep, nullptr);
  ASSERT_TRUE(sheep->isLastByteOfCodePoint());
  EXPECT_EQ(sheep->getSuffixLink(), eep);
  EXPECT_EQ(sheep->getOutputLink(), eep);

  // ---- Terminal nodes check ----
  for (const auto &pat : dataset) {
    auto cur = root;
    std::string bytes_debug;
    for (const char8_t c : pat) {
      cur = cur->getChild(static_cast<uint8_t>(c));
      ASSERT_NE(cur, nullptr);
      bytes_debug += static_cast<char>(c);  // debug output
    }
    EXPECT_TRUE(cur->isTerminalNode()) << "Pattern not terminal: " << bytes_debug;
    EXPECT_TRUE(ART::N256::isLeaf(cur->getChild(NULL_TERMINATOR)));
  }
}

// The counter behind the keys of literal_map_ used to be a static, so it was
// shared by every automaton in the process. Two tries built in the same
// program numbered their literals out of one sequence, the second one
// starting wherever the first one stopped, and neither could say how many
// literals it held. Nothing read the count, so nothing was visibly wrong, but
// the keys are the only handle on a literal and a dense range per instance is
// the only shape that lets them be used as an offset later.
//
// The two tries here are built one after the other in the same process, which
// is the case that was broken. With a static counter the second one's keys
// start at three.
TEST(TestAhoCorasick, LiteralIdsAreDensePerInstance) {
  auto first = AhoCorasick();
  // "he" twice, for two different patterns, to check that the count is
  // distinct literals and not calls to Insert.
  auto idx = 0U;
  for (auto &keyword : std::vector<std::u8string>{u8"he", u8"she", u8"his", u8"he"}) {
    first.Insert(reinterpret_cast<char *>(keyword.data()), keyword.size(), {idx++, 0});
  }
  EXPECT_EQ(first.NumUniqueLiterals(), 3U);

  auto second = AhoCorasick();
  idx         = 0U;
  for (auto &keyword : std::vector<std::u8string>{u8"alpha", u8"beta"}) {
    second.Insert(reinterpret_cast<char *>(keyword.data()), keyword.size(), {idx++, 0});
  }
  EXPECT_EQ(second.NumUniqueLiterals(), 2U);

  std::vector<TupleID> keys;
  for (const auto &entry : second.literal_map_) { keys.push_back(entry.first); }
  std::sort(keys.begin(), keys.end());
  EXPECT_EQ(keys, (std::vector<TupleID>{0, 1}));
}

}  // namespace aho_corasick

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
