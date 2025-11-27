#include "art/tree.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

TEST(TestArt, InsertAndQuery) {
  auto dataset = std::vector<std::string>{"abcdef", "xxxx", "aba", "ab"};
  for (auto &key : dataset) { key += '\0'; }
  auto load_key  = [&](TupleID tid, Key &key) { key.set(dataset[tid].c_str(), dataset[tid].size()); };
  auto check_key = [&](const TupleID tid, const Key &k) {
    Key cmp_key;
    cmp_key.set(dataset[tid].c_str(), dataset[tid].size());
    return k == cmp_key;
  };
  auto trie = ART_OLC::Tree();

  // Insert dataset
  Key key;
  auto t = trie.getThreadInfo();
  for (auto idx = 0U; idx < dataset.size(); idx++) {
    load_key(idx, key);
    trie.insert(key, idx, load_key, t);
    auto tid = trie.lookup(key, load_key, check_key, t);
    ASSERT_NE(tid, ART_OLC::Tree::INVALID_TID);
    ASSERT_TRUE(check_key(tid, key));
  }

  // Another separate search
  for (auto idx = 0U; idx < dataset.size(); idx++) {
    key.set(dataset[idx].c_str(), dataset[idx].size());
    auto tid = trie.lookup(key, load_key, check_key, t);
    ASSERT_EQ(tid, idx);
  }

  // Wrong search
  auto false_keywords = std::vector<std::string>{"abc", "xxx"};
  for (auto &keyword : false_keywords) {
    keyword += '\0';
    key.set(keyword.c_str(), keyword.size());
    auto tid = trie.lookup(key, load_key, check_key, t);
    if (tid != ART_OLC::Tree::INVALID_TID) { fmt::println("Keyword {}", keyword); }
    ASSERT_EQ(tid, ART_OLC::Tree::INVALID_TID);
  }
}

TEST(TestArt, InsertMany) {
  auto keywords  = std::vector<std::string>{};
  auto load_key  = [&](TupleID tid, Key &key) { key.set(keywords[tid].c_str(), keywords[tid].size()); };
  auto check_key = [&](const TupleID tid, const Key &k) {
    Key cmp_key;
    cmp_key.set(keywords[tid].c_str(), keywords[tid].size());
    return k == cmp_key;
  };
  auto trie = ART_OLC::Tree();

  Key key;
  auto t = trie.getThreadInfo();
  int len;
  char buf[512];
  auto f = fopen("test_words.txt", "r");

  // Prepare all keywords
  auto line = 0U;
  while (fgets(buf, sizeof(buf), f)) {
    len          = strlen(buf);
    buf[len - 1] = '\0';
    keywords.emplace_back(buf, len);
    load_key(line, key);
    trie.insert(key, line, load_key, t);
    auto tid = trie.lookup(key, load_key, check_key, t);
    ASSERT_NE(tid, ART_OLC::Tree::INVALID_TID);
    ASSERT_EQ(tid, line);
    ASSERT_TRUE(check_key(tid, key));
    line++;
  }

  // Test again
  auto test_f = fopen("test_words.txt", "r");
  line        = 0U;
  while (fgets(buf, sizeof(buf), test_f)) {
    len          = strlen(buf);
    buf[len - 1] = '\0';
    assert(len == strlen(buf) + 1);  // strlen() always ignore null terminator, i.e., \0
    key.set(buf, len);
    auto tid = trie.lookup(key, load_key, check_key, t);
    ASSERT_EQ(tid, line++);
    ASSERT_TRUE(check_key(tid, key));
  }
}

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
