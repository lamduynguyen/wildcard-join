#include "art/tree.h"

#include "gtest/gtest.h"

void loadKey(TupleID TupleID, Key &key) {
  // Store the key of the tuple into the key vector
  // Implementation is database specific
  key.setKeyLen(sizeof(TupleID));
  reinterpret_cast<uint64_t *>(&key[0])[0] = __builtin_bswap64(TupleID);
}

auto checkKey(const TupleID tid, const Key &k) -> bool {
  Key kt;
  loadKey(tid, kt);
  return k == kt;
}

TEST(TestArt, Insertion) {
  auto dataset   = std::vector<std::string>{"abcdef", "xxxx", "aba"};
  auto load_key  = [&](TupleID tid, Key &key) { key = dataset[tid].c_str(); };
  auto check_key = [&](const TupleID tid, const Key &k) { return k != dataset[tid].c_str(); };
  auto trie      = ART_OLC::Tree(load_key, check_key);

  // Dataset
  Key key;
  auto t = trie.getThreadInfo();
  for (auto idx = 0U; idx < dataset.size(); idx++) {
    load_key(idx, key);
    trie.insert(key, idx, t);
  }
}

TEST(TestArt, InsertAndQuery) {
  auto dataset   = std::vector<std::string>{"abcdef", "xxxx", "aba"};
  auto load_key  = [&](TupleID tid, Key &key) { key = dataset[tid].c_str(); };
  auto check_key = [&](const TupleID tid, const Key &k) { return k != dataset[tid].c_str(); };
  auto trie      = ART_OLC::Tree(load_key, check_key);

  // Insert dataset
  Key key;
  auto t = trie.getThreadInfo();
  for (auto idx = 0U; idx < dataset.size(); idx++) {
    load_key(idx, key);
    trie.insert(key, idx, t);
    auto tid = trie.lookup(key, t);
    ASSERT_NE(tid, ART_OLC::Tree::INVALID_TID);
    ASSERT_TRUE(check_key(tid, key));
  }

  // Another separate search
  for (auto idx = 0U; idx < dataset.size(); idx++) {
    key      = dataset[idx].c_str();
    auto tid = trie.lookup(key, t);
    ASSERT_EQ(tid, idx);
  }

  // Wrong search
  auto false_keywords = std::vector<std::string>{"aaaa", "aa", "ab", "abc", "xxx"};
  for (auto &keyword : false_keywords) {
    key      = keyword.c_str();
    auto tid = trie.lookup(key, t);
    ASSERT_EQ(tid, ART_OLC::Tree::INVALID_TID);
  }
}

// TEST(DISABLED_TestArt, InsertMany) {
//   auto trie = aho_corasick::ArtTree(1);
//   ASSERT_EQ(trie.TraverseTree(), 0);

//   int len;
//   char buf[512];
//   auto f = fopen("test_words.txt", "r");

//   // Insert all words
//   auto line = 0U;
//   while (fgets(buf, sizeof(buf), f)) {
//     len          = strlen(buf);
//     buf[len - 1] = '\0';
//     trie.Insert(reinterpret_cast<uint8_t *>(buf), len, {line, 0});
//     auto output = trie.Search(reinterpret_cast<uint8_t *>(buf), len);
//     if (output.empty()) { fmt::println("Line {}", line); }
//     ASSERT_EQ(output.size(), 1);
//     ASSERT_EQ(output[0].index, line++);
//     ASSERT_EQ(output[0].offset_within_pattern, 0);
//   }

//   // Test again
//   auto test_f = fopen("test_words.txt", "r");
//   line        = 0U;
//   while (fgets(buf, sizeof(buf), test_f)) {
//     len          = strlen(buf);
//     buf[len - 1] = '\0';
//     auto output  = trie.Search(reinterpret_cast<uint8_t *>(buf), len);
//     if (output.empty()) { fmt::println("Line {}", line); }
//     ASSERT_EQ(output.size(), 1);
//     ASSERT_EQ(output[0].index, line++);
//     ASSERT_EQ(output[0].offset_within_pattern, 0);
//   }
// }

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
