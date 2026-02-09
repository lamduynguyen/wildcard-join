#include "art/tree.h"
#include "test_words.h"

#include "gtest/gtest.h"

TEST(TestArt, InsertAndQuery) {
  auto dataset = std::vector<std::string>{"abcdef", "xxxx", "aba", "ab"};
  auto trie    = ART::Tree();

  // Insert dataset
  std::string key;
  auto t = trie.getThreadInfo();
  for (auto idx = 0U; idx < dataset.size(); idx++) {
    key = dataset[idx];
    trie.insert(key.c_str(), key.size(), true, [&]() { return idx; }, [](TupleID) {}, t);
    auto leaf = trie.lookup(key.c_str(), key.size(), true, t);
    ASSERT_NE(leaf, nullptr);
    ASSERT_EQ(dataset[leaf->auxIndex], key);
  }

  // Another separate search
  for (auto idx = 0U; idx < dataset.size(); idx++) {
    key       = dataset[idx];
    auto leaf = trie.lookup(key.c_str(), key.size(), true, t);
    ASSERT_NE(leaf, nullptr);
    ASSERT_EQ(leaf->auxIndex, idx);
  }

  // Wrong search
  auto false_keywords = std::vector<std::string>{"abc", "xxx"};
  for (auto &keyword : false_keywords) {
    auto leaf = trie.lookup(keyword.c_str(), keyword.size(), true, t);
    ASSERT_EQ(leaf, nullptr);
  }
}

TEST(TestArt, InsertMany) {
  auto keywords = LoadTestWords();
  auto trie     = ART::Tree();

  std::string key;
  auto t = trie.getThreadInfo();
  for (auto line = 0U; line < keywords.size(); line++) {
    key = keywords[line];
    trie.insert(key.c_str(), key.size(), false, [&]() { return line; }, [](TupleID) {}, t);
    auto leaf = trie.lookup(key.c_str(), key.size(), false, t);
    ASSERT_NE(leaf, nullptr);
    ASSERT_EQ(leaf->auxIndex, line);
    ASSERT_TRUE(keywords[leaf->auxIndex] == key);
  }

  // Test again
  for (auto line = 0U; line < keywords.size(); line++) {
    key       = keywords[line];
    auto leaf = trie.lookup(key.c_str(), key.size(), false, t);
    ASSERT_EQ(leaf->auxIndex, line++);
    ASSERT_TRUE(keywords[leaf->auxIndex] == key);
  }
}

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
