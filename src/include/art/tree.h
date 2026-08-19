#ifndef ART_OPTIMISTICLOCK_COUPLING_N_H
#define ART_OPTIMISTICLOCK_COUPLING_N_H

#include <cassert>
#include <functional>
#include <ranges>
#include <vector>

#include "art/node.h"
#include "common/utf8.h"

namespace aho_corasick {
class AhoCorasick;
}

namespace ART {

class Tree {
 private:
  friend class aho_corasick::AhoCorasick;

  N256 *const root;

  inline void yield(int count) const {
    if (count > 3) sched_yield();
#if defined(__x86_64__) || defined(__i386__)
    else           asm volatile("pause");
#elif defined(__aarch64__) || defined(__arm__)
    else           asm volatile("yield");
#else
    else           sched_yield();
#endif
  }

  // mustAppendNull is only read by the assert, so it is unused in a release
  // build and -Wunused-parameter says so.
  inline uint8_t getNextChar(const char *keyword, uint64_t keywordLen, [[maybe_unused]] bool mustAppendNull,
                             uint64_t index) {
    return (index >= keywordLen) ? (assert(mustAppendNull), NULL_TERMINATOR) : static_cast<uint8_t>(keyword[index]);
  }

 public:
  Tree() : root(N256::makeNode(true)) {}

  Tree(const Tree &) = delete;

  ~Tree() {
    N256::deleteChildren(root);
    N256::deleteNode(root);
  }

  template <typename NewKeyFn, typename UpsertFn>
  void insert(const char *keyword, uint64_t keywordLen, bool mustAppendNull, NewKeyFn &&insert_fn,
              UpsertFn &&upsert_fn) {
    assert(keywordLen > 0 && ((keyword[keywordLen - 1] == NULL_TERMINATOR) == !mustAppendNull));

    // Build isEndCodePoint array so intermediate nodes of multi-byte UTF-8
    // code points are labelled correctly during new-path insertion.
    // Index corresponds to trie level: root = 0, first byte of keyword = level 1.
    std::vector<bool> isEndCodePoint(keywordLen + mustAppendNull + 1, true);
    const char *end = keyword + keywordLen + mustAppendNull;
    for (auto ptr = keyword; ptr < end;) {
      auto result = umbra::Utf8::readCodePoint(ptr, end);
      for (const char *b = ptr; b < result.next - 1; ++b) { isEndCodePoint[b - keyword + 1] = false; }
      ptr = result.next;
    }

    int restartCount = 0;
  restart:
    if (restartCount++) yield(restartCount);
    bool needRestart = false;

    N256 *node       = nullptr;
    N256 *nextNode   = root;
    N256 *parentNode = nullptr;
    uint8_t parentKey, nodeKey = 0;
    uint64_t parentVersion = 0;
    uint32_t level         = 0;

    while (true) {
      parentNode = node;
      parentKey  = nodeKey;
      node       = nextNode;
      auto v     = node->readLockOrRestart(needRestart);
      if (needRestart) goto restart;

      nodeKey  = getNextChar(keyword, keywordLen, mustAppendNull, level);
      nextNode = node->getChild(nodeKey);
      node->readUnlockOrRestart(v, needRestart);
      if (needRestart) goto restart;

      if (nextNode == nullptr) {
        // Build the missing suffix of the path bottom-up, then attach it.
        auto generateVal = [&]() -> N256 * {
          N256 *lastNode = N256::setLeaf(Leaf::MakeLeaf(insert_fn()));
          if (level < keywordLen - 1 + mustAppendNull) {
            auto range = std::views::iota(level + 1, keywordLen + mustAppendNull) | std::views::reverse;
            for (auto idx : range) {
              auto n256 = N256::makeNode(isEndCodePoint[idx]);
              n256->insert(getNextChar(keyword, keywordLen, mustAppendNull, idx), lastNode);
              lastNode = n256;
            }
          }
          return lastNode;
        };
        node->insertAndUnlock(v, parentNode, parentVersion, parentKey, nodeKey, generateVal, needRestart);
        if (needRestart) goto restart;
        return;
      }

      if (parentNode != nullptr) {
        parentNode->readUnlockOrRestart(parentVersion, needRestart);
        if (needRestart) goto restart;
      }

      // Under the fully-expanded trie invariant (every keyword ends with '\0'
      // and leaves hang only off the '\0' child), a leaf is only reachable
      // when nodeKey == '\0', which means the full key has been consumed and
      // this is a duplicate insertion — unconditional upsert.
      if (N256::isLeaf(nextNode)) {
        node->upgradeToWriteLockOrRestart(v, needRestart);
        if (needRestart) goto restart;
        upsert_fn(N256::getLeaf(nextNode)->auxIndex);
        node->writeUnlock();
        return;
      }

      level++;
      parentVersion = v;
    }
  }
};

}  // namespace ART

#endif  // ART_OPTIMISTICLOCK_COUPLING_N_H
