#include "aho_corasick/aho_corasick.h"

#include "gtest/gtest.h"

TEST(TestArt, Basic) {
  auto trie = aho_corasick::ArtTree(1);
  ASSERT_EQ(trie.TraverseTree(), 0);
}

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
