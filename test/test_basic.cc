#include "art/tree.h"

#include "gtest/gtest.h"

void loadKey(TID tid, Key &key) {
  // Store the key of the tuple into the key vector
  // Implementation is database specific
  key.setKeyLen(sizeof(tid));
  reinterpret_cast<uint64_t *>(&key[0])[0] = __builtin_bswap64(tid);
}

TEST(TestArt, Basic) {
  auto trie = ART_OLC::Tree(loadKey);
}

TEST(DISABLED_TestArt, Insertion) {
  auto trie = aho_corasick::ArtTree(1);

  // Dataset
  auto dataset = std::vector<std::string>{"abcdef", "xxxx", "aba"};
  for (auto idx = 0U; idx < dataset.size(); idx++) {
    auto &keyword = dataset[idx];
    trie.Insert(reinterpret_cast<uint8_t *>(keyword.data()), keyword.size(), {idx, 0});
  }

  // Assertion trie structure
  ASSERT_EQ(trie.root_->type, ArtNodeType::NODE4);
  ASSERT_EQ(trie.root_->num_children, 2);
  ASSERT_EQ(trie.root_->prefix_len, 0);
  auto root = reinterpret_cast<ArtNode4 *>(trie.root_);
  EXPECT_THAT(root->keys, ::testing::ElementsAre('a', 'x', 0, 0));
  ASSERT_EQ(trie.TraverseTree(), 269);

  // Evaluate root's second child, corresponding for 'a'
  auto second_child = root->children[1];
  ASSERT_EQ(second_child->type, ArtNodeType::LEAF);
  auto second_child_leaf = reinterpret_cast<ArtLeaf *>(second_child);
  ASSERT_EQ(second_child_leaf->key_len, 4);
  ASSERT_EQ(memcmp(second_child_leaf->key, "xxxx", 4), 0);

  // Evaluate root's first child -- which should be a subtree -- corresponding for 'a'
  auto first_child = root->children[0];
  ASSERT_EQ(first_child->type, ArtNodeType::NODE4);
  ASSERT_EQ(first_child->num_children, 2);
  ASSERT_EQ(first_child->prefix_len, 1);
  ASSERT_EQ(first_child->prefix[0], 'b');
  auto first_child_inner = reinterpret_cast<ArtNode4 *>(first_child);
  auto subchild_key      = std::vector<std::string>{"aba", "abcdef"};
  EXPECT_THAT(first_child_inner->keys, ::testing::ElementsAre('a', 'c', 0, 0));
  for (auto idx = 0; idx < first_child->num_children; idx++) {
    auto subchild = first_child_inner->children[idx];
    ASSERT_EQ(subchild->type, ArtNodeType::LEAF);
    auto subchild_leaf = reinterpret_cast<ArtLeaf *>(subchild);
    ASSERT_EQ(subchild_leaf->key_len, subchild_key[idx].size());
    ASSERT_EQ(memcmp(subchild_leaf->key, subchild_key[idx].data(), subchild_key[idx].size()), 0);
  }
}

TEST(DISABLED_TestArt, InsertAndQuery) {
  auto trie = aho_corasick::ArtTree(1);
  ASSERT_EQ(trie.TraverseTree(), 0);

  // Dataset
  auto dataset = std::vector<std::string>{"abcdef", "xxxx", "aaa"};
  for (auto idx = 0U; idx < dataset.size(); idx++) {
    auto &keyword = dataset[idx];
    trie.Insert(reinterpret_cast<uint8_t *>(keyword.data()), keyword.size(), {idx, 0});
    auto output = trie.Search(reinterpret_cast<uint8_t *>(keyword.data()), keyword.size());
    ASSERT_EQ(output.size(), 1);
    ASSERT_EQ(output[0].index, idx);
    ASSERT_EQ(output[0].offset_within_pattern, 0);
  }

  // Try insert duplicate
  trie.Insert(reinterpret_cast<uint8_t *>(dataset[0].data()), dataset[0].size(), {6, 3});
  auto output = trie.Search(reinterpret_cast<uint8_t *>(dataset[0].data()), dataset[0].size());
  ASSERT_EQ(output.size(), 2);
  ASSERT_EQ(output[1].index, 6);
  ASSERT_EQ(output[1].offset_within_pattern, 3);

  // Another separate search (element 0 is duplicate and has different output indices)
  for (auto idx = 1U; idx < dataset.size(); idx++) {
    auto &keyword = dataset[idx];
    auto output   = trie.Search(reinterpret_cast<uint8_t *>(keyword.data()), keyword.size());
    ASSERT_EQ(output.size(), 1);
    ASSERT_EQ(output[0].index, idx);
    ASSERT_EQ(output[0].offset_within_pattern, 0);
  }

  // Test keywords
  auto false_keywords = std::vector<std::string>{"aaaa", "aa", "ab", "abc", "xxx"};
  for (auto &keyword : false_keywords) {
    ASSERT_TRUE(trie.Search(reinterpret_cast<uint8_t *>(keyword.data()), keyword.size()).empty());
  }
}

TEST(DISABLED_TestArt, InsertMany) {
  auto trie = aho_corasick::ArtTree(1);
  ASSERT_EQ(trie.TraverseTree(), 0);

  int len;
  char buf[512];
  auto f = fopen("test_words.txt", "r");

  // Insert all words
  auto line = 0U;
  while (fgets(buf, sizeof(buf), f)) {
    len          = strlen(buf);
    buf[len - 1] = '\0';
    trie.Insert(reinterpret_cast<uint8_t *>(buf), len, {line, 0});
    auto output = trie.Search(reinterpret_cast<uint8_t *>(buf), len);
    if (output.empty()) { fmt::println("Line {}", line); }
    ASSERT_EQ(output.size(), 1);
    ASSERT_EQ(output[0].index, line++);
    ASSERT_EQ(output[0].offset_within_pattern, 0);
  }

  // Test again
  auto test_f = fopen("test_words.txt", "r");
  line        = 0U;
  while (fgets(buf, sizeof(buf), test_f)) {
    len          = strlen(buf);
    buf[len - 1] = '\0';
    auto output  = trie.Search(reinterpret_cast<uint8_t *>(buf), len);
    if (output.empty()) { fmt::println("Line {}", line); }
    ASSERT_EQ(output.size(), 1);
    ASSERT_EQ(output[0].index, line++);
    ASSERT_EQ(output[0].offset_within_pattern, 0);
  }
}

}  // namespace aho_corasick

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
