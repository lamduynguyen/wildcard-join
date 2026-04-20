#ifndef ART_OPTIMISTICLOCK_COUPLING_N_H
#define ART_OPTIMISTICLOCK_COUPLING_N_H

#include <algorithm>
#include <cassert>
#include <functional>
#include <ranges>

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
    if (count > 3)
      sched_yield();
    else
      asm volatile("pause");
  }

  inline auto getNextChar(const char *keyword, uint64_t keywordLen, bool mustAppendNull, uint64_t index) {
    return (index >= keywordLen) ? (assert(mustAppendNull), NULL_TERMINATOR) : keyword[index];
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

    // -----------------------------
    // Step 1: Build isEndCodePoint array
    // IMPORTANT: the index of this array should correspond to the trie's level
    // That is, the level of the trie's root is 0, which corresponds to an empty string
    // For a literal of `abc` to be inserted into the trie, the levels of corresponding bytes are:
    //  (a = 1), (b = 2), (c = 3)
    // That's why we use `isEndCodePoint[b - keyword + 1] = false` rather than `isEndCodePoint[b - keyword] = false`
    // -----------------------------
    std::vector<bool> isEndCodePoint(keywordLen + mustAppendNull + 1, true);
    const char *end = keyword + keywordLen + mustAppendNull;
    for (auto ptr = keyword; ptr < end;) {
      auto result         = umbra::Utf8::readCodePoint(ptr, end);
      const char *nextPtr = result.next;
      // All bytes except the last are not end of code point
      for (const char *b = ptr; b < nextPtr - 1; ++b) { isEndCodePoint[b - keyword + 1] = false; }
      ptr = nextPtr;
    }

    // -----------------------------
    // Step 2: Trie insertion
    // -----------------------------
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
        auto generateVal = [&]() {
          auto lastNode = N256::setLeaf(
            Leaf::MakeLeaf(reinterpret_cast<const uint8_t *>(keyword), keywordLen, mustAppendNull, insert_fn()));
          if (level < keywordLen - 1 + mustAppendNull) {
            auto range = std::views::iota(level + 1, keywordLen + mustAppendNull) | std::views::reverse;
            for (auto idx : range) {
              auto aboveKey = getNextChar(keyword, keywordLen, mustAppendNull, idx);
              auto n256     = N256::makeNode(isEndCodePoint[idx]);
              n256->insert(aboveKey, lastNode);
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

      if (N256::isLeaf(nextNode)) {
        node->upgradeToWriteLockOrRestart(v, needRestart);
        if (needRestart) goto restart;

        // Matching key, call upsert() -- Only works with trie. With prefix tree/radix tree/variants, this is wrong
        auto leaf = reinterpret_cast<Leaf *>(N256::getLeaf(nextNode));
        if (leaf->equal(keyword, keywordLen, mustAppendNull)) {
          // upsert
          auto tid = N256::getLeaf(nextNode)->auxIndex;
          upsert_fn(tid);
          node->writeUnlock();
          return;
        }

        // Create new inner node to replace the leaf
        auto iterNode = N256::makeNode(isEndCodePoint[level]);
        node->change(nodeKey, iterNode);
        level++;
        assert(level < leaf->keyLen);  // prevent inserting when prefix of key exists already
        // Start inserting new intermediate nodes to represent shared prefix
        uint32_t prefixLength = 0;
        for (; (*leaf)[level + prefixLength] == getNextChar(keyword, keywordLen, mustAppendNull, level + prefixLength);
             ++prefixLength) {
          auto n256 = N256::makeNode(isEndCodePoint[level + prefixLength]);
          iterNode->insert(getNextChar(keyword, keywordLen, mustAppendNull, level + prefixLength), n256);
          iterNode = n256;
        }
        auto newNodeKey = getNextChar(keyword, keywordLen, mustAppendNull, level + prefixLength);
        assert(newNodeKey != (*leaf)[level + prefixLength]);  // should be different key here
        iterNode->insert(keyword[level + prefixLength],
                         N256::setLeaf(Leaf::MakeLeaf(reinterpret_cast<const uint8_t *>(keyword), keywordLen,
                                                      mustAppendNull, insert_fn())));
        iterNode->insert((*leaf)[level + prefixLength], nextNode);
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
