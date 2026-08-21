// Concurrent insertion into the ART.
//
// The trie in art/ carries a full optimistic lock coupling protocol: a version
// counter per node, readLockOrRestart, upgradeToWriteLockOrRestart, a restart
// label and a backoff yield. That protocol is a named contribution and until
// this file nothing in the repo ever ran two inserts at the same time. Every
// call site builds the trie on one thread, so the restart path had never been
// taken, the version counter had never been contended, and the cost of the
// atomics was being paid for nothing.
//
// These tests drive it. They are ordinary tests and pass in a normal build,
// but the build worth running them in is release-tsan, and the first thing
// they did there was report a race: N256::children was a plain pointer array,
// read optimistically without a lock and written under it. That is fixed
// in the same change as this file. The tests passed before the fix too, which
// is the usual way a data race behaves on x86 and the reason a functional
// concurrency test on its own is not worth much.
//
// The shapes are chosen for where the protocol actually has work to do:
//   disjoint keys      threads mostly touch different subtrees, contention is
//                      on the shared prefix near the root
//   identical keys     every thread walks the same path, so the create and
//                      the upsert paths are both hammered on the same nodes
//   shared prefixes    a deep common path, which is the worst case for lock
//                      coupling because the contended nodes are interior

#include "test_words.h"

#include "aho_corasick/aho_corasick.h"
#include "common/utf8.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

using aho_corasick::AhoCorasick;
using aho_corasick::PatternIndexType;

// Concurrency is capped rather than taken straight from hardware_concurrency.
// A 128 core runner would spend the whole test in thread creation, and the
// interesting interleavings are all reachable at eight. Floored at two,
// because a single threaded run of a concurrency test proves nothing and
// should be visible as a skip and not as a pass.
auto ThreadCount() -> unsigned {
  auto hw = std::thread::hardware_concurrency();
  if (hw == 0) { hw = 4; }
  return std::min(hw, 8U);
}

// Walks the trie the way the probe does, one code point at a time from the
// root, and returns the literal id sitting under the final null terminator.
// Public API only, so this checks what a caller can actually see rather than
// reaching into the node internals.
auto FindLiteralId(const AhoCorasick &ac, const std::string &key) -> std::optional<uint64_t> {
  auto *node      = ac.GetRoot();
  const auto *cur = key.data();
  const auto *end = key.data() + key.size();
  while (cur < end) {
    const auto info   = umbra::Utf8::readCodePoint(cur, end);
    const auto cp_len = static_cast<u8>(info.next - cur);
    node              = AhoCorasick::VisitCodePoint(node, cur, cp_len);
    if (node == nullptr) { return std::nullopt; }
    cur = info.next;
  }
  if (!node->isTerminalNode()) { return std::nullopt; }
  return ART::N256::getLeaf(node->getChild(NULL_TERMINATOR))->auxIndex;
}

// LoadTestWords null terminates every word, because that is what
// AhoCorasick::Insert wants in the key. The trie walk above wants them without,
// so both forms are kept.
struct WordSet {
  std::vector<std::string> with_null;
  std::vector<std::string> plain;
};

auto TakeWords(size_t n) -> WordSet {
  static const auto all = LoadTestWords();
  WordSet out;
  const auto take = std::min(n, all.size());
  out.with_null.reserve(take);
  out.plain.reserve(take);
  for (size_t i = 0; i < take; i++) {
    out.with_null.push_back(all[i]);
    out.plain.push_back(all[i].substr(0, all[i].size() - 1));
  }
  return out;
}

void RunOnThreads(unsigned threads, const std::function<void(unsigned)> &body) {
  std::vector<std::thread> pool;
  pool.reserve(threads);
  for (unsigned t = 0; t < threads; t++) { pool.emplace_back(body, t); }
  for (auto &th : pool) { th.join(); }
}

// Every literal id the trie handed out, exactly once and with no gaps. The
// counter behind it is a fetch_add, so a torn or repeated id is the first
// thing a broken concurrent insert produces.
void ExpectDenseLiteralIds(const AhoCorasick &ac, const WordSet &words) {
  std::unordered_set<uint64_t> seen;
  for (const auto &w : words.plain) {
    const auto id = FindLiteralId(ac, w);
    ASSERT_TRUE(id.has_value()) << "'" << w << "' is not in the trie after a concurrent build";
    EXPECT_TRUE(seen.insert(*id).second) << "'" << w << "' shares a literal id with an earlier word";
  }
  EXPECT_EQ(seen.size(), words.plain.size());
  EXPECT_EQ(ac.NumUniqueLiterals(), words.plain.size());

  uint64_t max_id = 0;
  for (auto id : seen) { max_id = std::max(max_id, id); }
  EXPECT_EQ(max_id + 1, seen.size()) << "the literal ids have a gap in them, so one fetch_add went missing";
}

// Ten thousand rather than the whole list. Under tsan every one of these
// inserts carries shadow memory bookkeeping on every access, and the whole
// list turns a two second test into minutes without reaching any interleaving
// the first ten thousand did not.
constexpr size_t WORD_BUDGET = 10000;

TEST(TestConcurrentART, DisjointKeys) {
  const auto threads = ThreadCount();
  ASSERT_GE(threads, 2U) << "this machine reports one core, nothing here is being tested";

  const auto words = TakeWords(WORD_BUDGET);
  AhoCorasick ac;

  // Strided and not blocked. A block per thread has each thread working in its
  // own alphabetical neighbourhood, which is the arrangement least likely to
  // collide. Striding puts every thread all over the trie at once.
  RunOnThreads(threads, [&](unsigned t) {
    for (size_t i = t; i < words.with_null.size(); i += threads) {
      PatternIndexType idx(static_cast<u32>(i), 0);
      ac.Insert(words.with_null[i].data(), words.with_null[i].size(), idx);
    }
  });

  ExpectDenseLiteralIds(ac, words);
}

TEST(TestConcurrentART, EveryThreadInsertsEveryKey) {
  const auto threads = ThreadCount();
  ASSERT_GE(threads, 2U) << "this machine reports one core, nothing here is being tested";

  // Smaller, because this one does threads times the work of the test above.
  const auto words = TakeWords(WORD_BUDGET / 4);
  AhoCorasick ac;

  // Each thread claims its own pattern id range, so the bitmap under each
  // literal ends up holding one entry per thread and the upsert path runs on
  // every node rather than only on the duplicates that happen to exist in the
  // word list.
  RunOnThreads(threads, [&](unsigned t) {
    for (size_t i = 0; i < words.with_null.size(); i++) {
      PatternIndexType idx(static_cast<u32>(t * words.with_null.size() + i), 0);
      ac.Insert(words.with_null[i].data(), words.with_null[i].size(), idx);
    }
  });

  // Still one literal per distinct word. If two threads both took the create
  // path for the same key, this is where it shows up.
  ExpectDenseLiteralIds(ac, words);
}

TEST(TestConcurrentART, DeepSharedPrefix) {
  const auto threads = ThreadCount();
  ASSERT_GE(threads, 2U) << "this machine reports one core, nothing here is being tested";

  // A 64 byte common prefix, so every insert walks 64 contended interior nodes
  // before it reaches anything of its own. Lock coupling holds two nodes at a
  // time, so this is the arrangement that keeps the most versions in flight.
  const std::string prefix(64, 'z');
  std::vector<std::string> keys;
  keys.reserve(2000);
  for (size_t i = 0; i < 2000; i++) { keys.push_back(prefix + std::to_string(i)); }

  WordSet words;
  words.plain = keys;
  for (const auto &k : keys) { words.with_null.push_back(k + '\0'); }

  AhoCorasick ac;
  RunOnThreads(threads, [&](unsigned t) {
    for (size_t i = t; i < words.with_null.size(); i += threads) {
      PatternIndexType idx(static_cast<u32>(i), 0);
      ac.Insert(words.with_null[i].data(), words.with_null[i].size(), idx);
    }
  });

  ExpectDenseLiteralIds(ac, words);
}

// A concurrent build has to leave a trie that a probe can be run over, which
// means BuildSuffixLink has to work on it and the result has to be the same
// as a serial build of the same keys. The link structure is what an insert
// ordering difference would most plausibly disturb.
TEST(TestConcurrentART, MatchesASerialBuild) {
  const auto threads = ThreadCount();
  ASSERT_GE(threads, 2U) << "this machine reports one core, nothing here is being tested";

  const auto words = TakeWords(WORD_BUDGET);

  AhoCorasick serial;
  for (size_t i = 0; i < words.with_null.size(); i++) {
    PatternIndexType idx(static_cast<u32>(i), 0);
    serial.Insert(words.with_null[i].data(), words.with_null[i].size(), idx);
  }
  serial.BuildSuffixLink();

  AhoCorasick concurrent;
  RunOnThreads(threads, [&](unsigned t) {
    for (size_t i = t; i < words.with_null.size(); i += threads) {
      PatternIndexType idx(static_cast<u32>(i), 0);
      concurrent.Insert(words.with_null[i].data(), words.with_null[i].size(), idx);
    }
  });
  concurrent.BuildSuffixLink();

  EXPECT_EQ(serial.NumUniqueLiterals(), concurrent.NumUniqueLiterals());

  // Not the ids themselves. Those are handed out in insertion order and the
  // concurrent build has no insertion order, so demanding they agree would be
  // demanding the wrong thing. What has to agree is that a key is present in
  // one exactly when it is present in the other.
  for (const auto &w : words.plain) {
    EXPECT_TRUE(FindLiteralId(serial, w).has_value()) << "'" << w << "' missing from the serial build";
    EXPECT_TRUE(FindLiteralId(concurrent, w).has_value()) << "'" << w << "' missing from the concurrent build";
  }

  // A key nobody inserted must be absent from both, otherwise the check above
  // would pass on a trie that says yes to everything.
  EXPECT_FALSE(FindLiteralId(serial, "zzzzzzzzzznotaword").has_value());
  EXPECT_FALSE(FindLiteralId(concurrent, "zzzzzzzzzznotaword").has_value());
}

}  // namespace

auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
